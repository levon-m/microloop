#include "encoder_io.h"

namespace EncoderIO {

// MCP23017 instance
static Adafruit_MCP23X17 mcp;

// Interrupt pin (Teensy pin connected to MCP23017 INTA or INTB in mirror mode)
static const uint8_t INT_PIN = 39;

// Event queue to pass captured states from ISR to main loop
struct EncoderEvent {
    uint16_t capturedPins;  // All 16 pins captured at interrupt time
    uint32_t timestamp;     // When the interrupt fired
};

// Circular buffer for events (power of 2 for fast modulo)
static const uint8_t EVENT_QUEUE_SIZE = 64;
static volatile EncoderEvent eventQueue[EVENT_QUEUE_SIZE];
static volatile uint8_t eventQueueHead = 0;  // Write index (ISR)
static volatile uint8_t eventQueueTail = 0;  // Read index (main loop)

// ISR: Called when MCP23017 detects any pin change
// CRITICAL: Must read INTCAP immediately to capture state before next interrupt overwrites it
static void encoderISR() {
    // Read INTCAP registers directly to get the captured pin states
    // This is the state at the moment the interrupt fired
    // INTCAPA = 0x10, INTCAPB = 0x11 (MCP23017 register addresses)
    Wire.beginTransmission(0x20);
    Wire.write(0x10);
    Wire.endTransmission(false);
    Wire.requestFrom(0x20, 2);

    uint8_t intcapA = Wire.read();
    uint8_t intcapB = Wire.read();
    uint16_t captured = ((uint16_t)intcapB << 8) | intcapA;

    // Add to queue (fast, no decoding)
    uint8_t nextHead = (eventQueueHead + 1) & (EVENT_QUEUE_SIZE - 1);
    if (nextHead != eventQueueTail) {
        eventQueue[eventQueueHead].capturedPins = captured;
        eventQueue[eventQueueHead].timestamp = millis();
        eventQueueHead = nextHead;
    }
}

// Encoder pin configurations
static const EncoderPins encoderPins[4] = {
    {4, 3, 2},  // Encoder 1: A=GPA4, B=GPA3, SW=GPA2
    {8, 9, 10}, // Encoder 2: A=GPB0, B=GPB1, SW=GPB2
    {11, 12, 13}, // Encoder 3: A=GPB3, B=GPB4, SW=GPB5
    {7, 6, 5}   // Encoder 4: A=GPA7, B=GPA6, SW=GPA5
};

// Encoder state tracking
static EncoderState encoders[4] = {};

// Debounce time for buttons (ms)
static const uint32_t DEBOUNCE_TIME_MS = 20;

// Quadrature decoder lookup table
// Index: [prevState][currState] -> returns direction (-1, 0, +1)
// prevState/currState: 2-bit value (B << 1 | A)
static const int8_t QUADRATURE_TABLE[4][4] = {
    // From 00 (both low)
    { 0, +1, -1,  0}, // To: 00, 01, 10, 11
    // From 01 (A high)
    {-1,  0,  0, +1}, // To: 00, 01, 10, 11
    // From 10 (B high)
    {+1,  0,  0, -1}, // To: 00, 01, 10, 11
    // From 11 (both high)
    { 0, -1, +1,  0}  // To: 00, 01, 10, 11
};

bool begin() {
    // Initialize I2C on Wire (shared with Audio Shield)
    Wire.begin();
    Wire.setClock(400000); // 400kHz I2C

    // Initialize MCP23017
    if (!mcp.begin_I2C(0x20, &Wire)) {
        return false;
    }

    // Configure all encoder pins as inputs with pull-ups
    for (int i = 0; i < 4; i++) {
        mcp.pinMode(encoderPins[i].pinA, INPUT_PULLUP);
        mcp.pinMode(encoderPins[i].pinB, INPUT_PULLUP);
        mcp.pinMode(encoderPins[i].pinSW, INPUT_PULLUP);

        // Read initial state
        bool a = mcp.digitalRead(encoderPins[i].pinA);
        bool b = mcp.digitalRead(encoderPins[i].pinB);
        encoders[i].lastState = (b << 1) | a;
        encoders[i].buttonLastState = mcp.digitalRead(encoderPins[i].pinSW);
        encoders[i].position = 0;
        encoders[i].lastDebounceTime = 0;
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

    // Clear any pending interrupts by reading the capture registers
    mcp.getLastInterruptPin();

    // Attach Teensy interrupt to MCP23017 INT pin
    pinMode(INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(INT_PIN), encoderISR, FALLING);

    return true;
}

void update() {
    // Process all queued events (ISR captures them directly)
    while (eventQueueTail != eventQueueHead) {
        // Get next event from queue (copy volatile data to local)
        uint16_t pins = eventQueue[eventQueueTail].capturedPins;
        uint32_t timestamp = eventQueue[eventQueueTail].timestamp;
        eventQueueTail = (eventQueueTail + 1) & (EVENT_QUEUE_SIZE - 1);

        // Process all encoders with this captured state
        for (int i = 0; i < 4; i++) {
            // Extract A/B pins from the 16-bit captured value
            bool a = (pins >> encoderPins[i].pinA) & 1;
            bool b = (pins >> encoderPins[i].pinB) & 1;
            uint8_t currState = (b << 1) | a;

            // Check if state changed
            if (currState != encoders[i].lastState) {
                // Decode direction using lookup table
                int8_t dir = QUADRATURE_TABLE[encoders[i].lastState][currState];

                if (dir != 0) {
                    encoders[i].position += dir;
                }

                encoders[i].lastState = currState;
            }

            // Check button state from captured pins
            bool buttonState = (pins >> encoderPins[i].pinSW) & 1;

            // Detect button press (edge: HIGH -> LOW, active low)
            if (buttonState == LOW && encoders[i].buttonLastState == HIGH) {
                // Debounce check
                if ((timestamp - encoders[i].lastDebounceTime) > DEBOUNCE_TIME_MS) {
                    encoders[i].buttonPressed = true;
                    encoders[i].lastDebounceTime = timestamp;
                }
            }

            encoders[i].buttonLastState = buttonState;
        }
    }
}

int32_t getPosition(uint8_t encoderNum) {
    if (encoderNum < 4) {
        return encoders[encoderNum].position;
    }
    return 0;
}

bool getButton(uint8_t encoderNum) {
    if (encoderNum < 4) {
        bool pressed = encoders[encoderNum].buttonPressed;
        encoders[encoderNum].buttonPressed = false;  // Consume the button press
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