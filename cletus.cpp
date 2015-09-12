#include <vector>
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <cstdlib>
#include <cmath>
#include <unordered_map>

constexpr int BITS_PER_BYTE           = 8;
constexpr int LOW_AFTER_SAMPLES       = 10;
constexpr int SIGNAL_THRESHOLD        = 10;
// 50 ticksworth of silence is the end of a button push
constexpr int SILENCE_TICK_LENGTH     = 50;
// 1.5 * ticklength indicates a "long" tick 
constexpr float LONG_TICK_THRESHOLD   = 1.5;

class Cletus {
private:
    std::unordered_map<int, std::string> buttons {
        {0x0f, "1"},
        {0x21, "2"},
        {0x2d, "3"},
        {0x17, "Status"},
        {0x05, "4"},
        {0x2b, "5"},
        {0x27, "6"},
        {0x11, "Bypass"},
        {0x09, "7"},
        {0x03, "8"},
        {0x1d, "9"},
        {0x1b, "0"}
    };
    enum class CletusState {QUIET, MARK, SPACE, ABORT, NEEDCHANGE, TICKWAIT};
    CletusState state  = CletusState::QUIET;
    uint64_t lastchange;
    uint64_t ticklength = 0;
    uint64_t sample_num = 0;
    int lastlevel = 0;
    int working_byte = 0;
    std::vector<int> bytes;
    int gotbits = 0;
    bool debug = false;
    
private:
    void dumpcode();
    void foundbit(int b);
    void reset();

public:
    Cletus() {};
    ~Cletus() {};
    void processSample(int level);
};

void Cletus::foundbit(int b) {
    if (debug) std::cout << "BIT:" << b << std::endl;
    // shift a bit into our store. 
    working_byte  = ((working_byte << 1) | (b & 1));
    // Once we have a 
    // byte's worth, push it into our vector
    if (++gotbits % BITS_PER_BYTE == 0) {
        bytes.push_back(working_byte);
        working_byte = 0;
    }
}


void Cletus::dumpcode() {
    if (bytes.size() < 1) return;

    for (int val : bytes) {
        try {
            std::string button = buttons.at(val);
            std::cout << button << " ";
        } catch (std::exception e) {
            std::cout << "Invalid (" << std::hex << val << ") ";
        }
    }
    std::cout << std::endl;
}


void Cletus::reset() {
    if (debug) std::cout << "RESET" << std::endl;
    dumpcode();
    bytes.clear();
    gotbits = 0;
    working_byte = 0;
    sample_num = 0;
    lastlevel = 0;
    lastchange = 0;
    state = CletusState::QUIET;
    ticklength = 0;
}

void Cletus::processSample(int level) {

    switch(state) {
        case(CletusState::QUIET):
            // All is quiet. Keep going until we get a signal
            if (level) {
                state = CletusState::MARK;
                lastchange = sample_num = 0;
            }
            break;
        case(CletusState::MARK):
            // We have our first high signal.
            // Read until we hit the next low and 
            // record the time as a "tick"
            if (!level) {
                state = CletusState::SPACE;
                if (0 == ticklength) {
                    ticklength = sample_num - lastchange;
                    if (debug) std::cout << "Tick: " << ticklength << std::endl;
                    foundbit(0);
                } else if (sample_num - lastchange > (ticklength * LONG_TICK_THRESHOLD)) {
                    foundbit(1);
                } else {
                    foundbit(0);
                }
                if (debug) std::cout << "SL: " << std::dec << sample_num << " L" << (sample_num - lastchange) << std::endl;
            }
            break;
        case(CletusState::SPACE):
            if ((sample_num - lastchange) > (ticklength * SILENCE_TICK_LENGTH)) {
                state = CletusState::ABORT;
            } else if (level) {
                state = CletusState::MARK;
                lastchange = sample_num;
            }
            break;
        case(CletusState::ABORT):
            // Start agin
            reset();
            break;
        default:
            break;
    }
    sample_num++;
}


void usage() {
    std::cerr << "cletus - vintage ITI keypad decoding tool" << std::endl;
    std::cerr << "\te.g. rtl_sdr -g 10 -f 340900000 -s 2000000 - | ./cletus" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string filename = "/dev/stdin";
    if (argc > 1) {
        filename = argv[1];
    }
    usage();
    std::ifstream data(filename, std::ios::binary|std::ios::in);
    uint8_t buf[2];
    int level = 0;
    int lowcount = 0;
    int lowlen = LOW_AFTER_SAMPLES;
    Cletus cletus;
    
    while(!data.eof()) {
        data.read((char *)buf,2);
        int i = buf[0] - 127;
        int q = buf[1] - 127;
        int sig = sqrt(i * i + q * q);

        // filter out everything but a on or off signal.
        if (sig > SIGNAL_THRESHOLD) {
            level = 1;
            lowcount = 0;
        } else if (level == 1) {
            if (lowcount++ > lowlen) {
                level = 0;
                lowcount = 0;
            }
        }
        cletus.processSample(level);
    }
    
    data.close();
    return 0;
}
