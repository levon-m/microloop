#include "mcp_io.h"
#include <TeensyThreads.h>

// Debug mode: Set to 1 to enable detailed button press logging
#define MCP_DEBUG 0

namespace McpIO {

// MCP23017 instance
static Adafruit_MCP23X17 mcp;

// MCP I2C address
static constexpr uint8_t MCP_ADDRESS = 0x20;

// Interrupt pin (Teensy pin connected to MCP23017 INTA or INTB in mirror mode)
static constexpr uint8_t INT_PIN = 39;

// MCP23017 register addresses
static constexpr uint8_t INTCAPA_REG = 0x10;  // Interrupt capture register for port A
static constexpr uint8_t INTCAPB_REG = 0x11;  // Interrupt capture register for port B

// Event queue to pass captured states from ISR to main loop
struct McpEvent {
    uint16_t pins;       // All 16 pins captured at interrupt time (INTCAP A/B)
    uint32_t timestamp;  // When the interrupt fired (milliseconds)
};

// Circular buffer for events (power of 2 for fast modulo with bitwise AND)
static constexpr uint8_t EVENT_QUEUE_SIZE = 64;
static volatile McpEvent eventQueue[EVENT_QUEUE_SIZE];
static volatile uint8_t eventQueueHead = 0;  // Write index (ISR)
static volatile uint8_t eventQueueTail = 0;  // Read index (thread/update)

// ISR: Called when MCP23017 detects any pin change
// CRITICAL: Must read INTCAP immediately to capture state before next interrupt overwrites it
// CONSTRAINT: Keep I2C in ISR - INTCAP registers are single-buffered snapshots
static void mcpISR() {
    // Read INTCAP registers directly to get the captured pin states
    // This is the state at the moment the interrupt fired
    // INTCAPA = 0x10, INTCAPB = 0x11 (MCP23017 register addresses)
    // Use auto-increment to read both in one I2C transaction (~20µs total)
    Wire.beginTransmission(MCP_ADDRESS);
    Wire.write(INTCAPA_REG);  // Start at INTCAPA
    Wire.endTransmission(false);  // Repeated start
    Wire.requestFrom(MCP_ADDRESS, 2);  // Read INTCAPA then INTCAPB

    uint8_t intcapA = Wire.read();
    uint8_t intcapB = Wire.read();
    uint16_t captured = ((uint16_t)intcapB << 8) | intcapA;

    // Add to queue (fast, no decoding)
    uint8_t nextHead = (eventQueueHead + 1) & (EVENT_QUEUE_SIZE - 1);
    if (nextHead != eventQueueTail) {
        eventQueue[eventQueueHead].pins = captured;
        eventQueue[eventQueueHead].timestamp = millis();
        eventQueueHead = nextHead;
    }
    // If queue full (nextHead == tail), drop oldest event by overwriting
}

// Encoder pin configurations
static constexpr EncoderPins encoderPins[4] = {
    {4, 3, 2},    // Encoder 1: A=GPA4, B=GPA3, SW=GPA2
    {8, 9, 10},   // Encoder 2: A=GPB0, B=GPB1, SW=GPB2
    {11, 12, 13}, // Encoder 3: A=GPB3, B=GPB4, SW=GPB5
    {7, 6, 5}     // Encoder 4: A=GPA7, B=GPA6, SW=GPA5
};

// Extra MCP button pin assignments (example, adjust as needed)
// Using remaining available MCP pins
static constexpr uint8_t auxButtonPins[4] = {
    0,   // AUX 0: GPA0
    1,   // AUX 1: GPA1
    14,  // AUX 2: GPB6
    15   // AUX 3: GPB7
};

// Encoder state tracking
static EncoderState encoders[4] = {};

// Extra button state tracking
static AuxButtonState auxButtons[4] = {};

// Debounce time for all buttons (NeoKey uses 20ms)
static constexpr uint32_t DEBOUNCE_MS = 20;

// Quadrature decoder lookup table
// Index: [prevState][currState] -> returns direction (-1, 0, +1)
// prevState/currState: 2-bit value (B << 1 | A)
static constexpr int8_t QUADRATURE_TABLE[4][4] = {
    // From 00 (both low)
    { 0, +1, -1,  0}, // To: 00, 01, 10, 11
    // From 01 (A high)
    {-1,  0,  0, +1}, // To: 00, 01, 10, 11
    // From 10 (B high)
    {+1,  0,  0, -1}, // To: 00, 01, 10, 11
    // From 11 (both high)
    { 0, -1, +1,  0}  // To: 00, 01, 10, 11
};

// Helper: Pop event from queue (thread-safe)
static bool popEvent(McpEvent &out) {
    bool success = false;
    noInterrupts();
    if (eventQueueHead != eventQueueTail) {
        // Manual copy to avoid volatile assignment issues
        out.pins = eventQueue[eventQueueTail].pins;
        out.timestamp = eventQueue[eventQueueTail].timestamp;
        eventQueueTail = (eventQueueTail + 1) & (EVENT_QUEUE_SIZE - 1);
        success = true;
    }
    interrupts();
    return success;
}

// Helper: Process button with NeoKey-style debouncing
// Updates lastState, lastEventTime, and sets pressedFlag on stable press edge
static void processButton(bool &lastState, uint32_t &lastEventTime, bool &pressedFlag,
                          bool rawPressed, uint32_t now, uint8_t debugIndex, const char* debugName) {
#if !MCP_DEBUG
    // Suppress unused parameter warnings in non-debug builds
    (void)debugIndex;
    (void)debugName;
#endif

    if (rawPressed != lastState) {
#if MCP_DEBUG
        Serial.printf("%s[%d] RAW CHANGE: %s at %lu ms\n",
                      debugName, debugIndex,
                      rawPressed ? "PRESSED" : "RELEASED", now);
#endif

        if (now - lastEventTime >= DEBOUNCE_MS) {
            lastEventTime = now;
            lastState = rawPressed;

            if (rawPressed) {
                // Stable press edge detected
                pressedFlag = true;

#if MCP_DEBUG
                Serial.printf("%s[%d] DEBOUNCED PRESS at %lu ms\n",
                              debugName, debugIndex, now);
#endif
            }
            // On release edge, do nothing event-wise (just update state)
        }
        // If within DEBOUNCE_MS, ignore the change
    }
}

// Process a single event (quadrature + button debounce)
static void processEvent(const McpEvent &ev) {
    // Process all encoders with this captured state
    for (int i = 0; i < 4; i++) {
        // 1. Quadrature decoding (rotation)
        // Extract A/B pins from the 16-bit captured value
        bool a = (ev.pins >> encoderPins[i].pinA) & 1;
        bool b = (ev.pins >> encoderPins[i].pinB) & 1;
        uint8_t currState = (b << 1) | a;

        // Check if state changed
        if (currState != encoders[i].lastQuadState) {
            // Decode direction using lookup table
            int8_t dir = QUADRATURE_TABLE[encoders[i].lastQuadState][currState];

            if (dir != 0) {
                encoders[i].position += dir;
            }

            encoders[i].lastQuadState = currState;
        }

        // 2. Encoder button debouncing (NeoKey-style)
        // Extract button pin from captured value
        bool rawBit = (ev.pins >> encoderPins[i].pinSW) & 1;
        bool rawPressed = (rawBit == 0);  // Active-low buttons

        processButton(encoders[i].buttonLastState,
                     encoders[i].buttonLastEventTime,
                     encoders[i].buttonPressed,
                     rawPressed,
                     ev.timestamp,
                     i,
                     "ENC");
    }

    // Process extra MCP buttons
    for (int j = 0; j < 4; j++) {
        bool rawBit = (ev.pins >> auxButtonPins[j]) & 1;
        bool rawPressed = (rawBit == 0);  // Active-low buttons

        processButton(auxButtons[j].lastState,
                     auxButtons[j].lastEventTime,
                     auxButtons[j].pressedFlag,
                     rawPressed,
                     ev.timestamp,
                     j,
                     "AUX");
    }
}

bool begin() {
    // Initialize I2C on Wire (shared with Audio Shield)
    Wire.begin();
    Wire.setClock(400000); // 400kHz I2C

    // Initialize MCP23017
    if (!mcp.begin_I2C(MCP_ADDRESS, &Wire)) {
        Serial.println("ERROR: McpIO - MCP23017 not detected on I2C!");
        return false;
    }

    // Configure all encoder pins as inputs with pull-ups
    for (int i = 0; i < 4; i++) {
        mcp.pinMode(encoderPins[i].pinA, INPUT_PULLUP);
        mcp.pinMode(encoderPins[i].pinB, INPUT_PULLUP);
        mcp.pinMode(encoderPins[i].pinSW, INPUT_PULLUP);

        // Read initial state for quadrature
        bool a = mcp.digitalRead(encoderPins[i].pinA);
        bool b = mcp.digitalRead(encoderPins[i].pinB);
        encoders[i].lastQuadState = (b << 1) | a;

        // Initialize button state
        bool swBit = mcp.digitalRead(encoderPins[i].pinSW);
        encoders[i].buttonLastState = (swBit == 0);  // Convert to pressed/released
        encoders[i].buttonLastEventTime = 0;
        encoders[i].buttonPressed = false;
        encoders[i].position = 0;
    }

    // Configure extra MCP button pins as inputs with pull-ups
    for (int j = 0; j < 4; j++) {
        mcp.pinMode(auxButtonPins[j], INPUT_PULLUP);

        // Initialize button state
        bool bit = mcp.digitalRead(auxButtonPins[j]);
        auxButtons[j].lastState = (bit == 0);  // Convert to pressed/released
        auxButtons[j].lastEventTime = 0;
        auxButtons[j].pressedFlag = false;
    }

    // Enable interrupt-on-change for all pins
    // Configure MCP23017 to trigger INTA on any pin change
    mcp.setupInterrupts(true, false, LOW);  // Mirror interrupts, open-drain off, active low

    // Enable interrupt-on-change for all encoder pins (A, B, and SW)
    for (int i = 0; i < 4; i++) {
        mcp.setupInterruptPin(encoderPins[i].pinA, CHANGE);
        mcp.setupInterruptPin(encoderPins[i].pinB, CHANGE);
        mcp.setupInterruptPin(encoderPins[i].pinSW, CHANGE);
    }

    // Enable interrupt-on-change for all extra button pins
    for (int j = 0; j < 4; j++) {
        mcp.setupInterruptPin(auxButtonPins[j], CHANGE);
    }

    // Clear any pending interrupts by reading the capture registers
    mcp.getLastInterruptPin();

    // Attach Teensy interrupt to MCP23017 INT pin
    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), mcpISR, FALLING);

    Serial.println("McpIO: MCP23017 initialized (I2C 0x20, INT on pin 36, 4 encoders + 4 aux buttons)");
    return true;
}

void threadLoop() {
    for (;;) {
        // 1. Pop events from queue
        McpEvent ev;
        bool hasEvent = popEvent(ev);

        if (hasEvent) {
            processEvent(ev);  // Decode quadrature + debounce buttons
        } else {
            threads.delay(2);  // Small sleep when idle
        }
    }
}

void update() {
    // Alternative to threadLoop: Drain all pending events when called
    // Use this if you don't want a dedicated MCP thread
    McpEvent ev;
    while (popEvent(ev)) {
        processEvent(ev);
    }
}

int32_t getPosition(uint8_t encoderNum) {
    if (encoderNum < 4) {
        return encoders[encoderNum].position;
    }
    return 0;
}

bool getEncoderButton(uint8_t encoderNum) {
    if (encoderNum < 4) {
        bool pressed;
        noInterrupts();
        pressed = encoders[encoderNum].buttonPressed;
        encoders[encoderNum].buttonPressed = false;  // Consume the button press
        interrupts();

#if MCP_DEBUG
        if (pressed) {
            Serial.printf("getEncoderButton(%d) consumed press at %lu ms\n",
                          encoderNum, millis());
        }
#endif

        return pressed;
    }
    return false;
}

bool getAuxButton(uint8_t buttonNum) {
    if (buttonNum < 4) {
        bool pressed;
        noInterrupts();
        pressed = auxButtons[buttonNum].pressedFlag;
        auxButtons[buttonNum].pressedFlag = false;  // Consume the button press
        interrupts();

#if MCP_DEBUG
        if (pressed) {
            Serial.printf("getAuxButton(%d) consumed press at %lu ms\n",
                          buttonNum, millis());
        }
#endif

        return pressed;
    }
    return false;
}

void resetPosition(uint8_t encoderNum) {
    if (encoderNum < 4) {
        encoders[encoderNum].position = 0;
    }
}

}
