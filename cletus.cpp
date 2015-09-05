#include <vector>
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <cstdlib>
#include <cmath>
#include <unordered_map>

constexpr int BITS_PER_BYTE         = 8;
constexpr int LOW_AFTER_SAMPLES     = 10;
constexpr int SIGNAL_THRESHOLD      = 10;

class Cletus {
private:
/*
1 ssssllll
2 sslssssl
3 sslsllsl
status ssslslll
4 ssssslsl
5 sslslsll
6 sslsslll
bypass ssslsssl
7 sssslssl
8 ssssssll
9 ssslllsl
0 sssllsll
*/

    std::unordered_map<int, std::string> buttons {
        {0x0f, "1"},
        {0x21, "2"},
        {0x2c, "3"},
        {0x17, "Status"},
        {0x05, "4"},
    };
    enum class CletusState {QUIET, FIRSTMARK, INMESSAGE, ABORT, NEEDCHANGE, TICKWAIT};
    CletusState state  = CletusState::QUIET;
    uint64_t lastchange;
    uint64_t ticklength;
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
    // We need two byes, the first having an upper nibble of
    // 0xf, otherwise this is probably bullshit
    if (bytes.size() >= 2 && ((bytes.at(0) & 0xf0) == 0xf0)) {
        for (int val : bytes) {
            std::cout << std::hex << val << " ";
        }
        std::cout << std::endl;
    }
}


void Cletus::reset() {
    dumpcode();
    bytes.clear();
    gotbits = 0;
    working_byte = 0;
    sample_num = 0;
    lastlevel = 0;
    lastchange = 0;
    state = CletusState::QUIET;
}

void Cletus::processSample(int level) {

    switch(state) {
        case(CletusState::QUIET):
            // All is quiet. Keep going until we get a signal
            if (level) {
                state = CletusState::FIRSTMARK;
                lastchange = sample_num = 0;
            }
            break;
        case(CletusState::FIRSTMARK):
            // We have our first high signal.
            // Read until we hit the next low and 
            // record the time as a "tick"
            // and note that the low to high transition
            // is a one
            if (!level) {
                state = CletusState::INMESSAGE;
                ticklength = sample_num - lastchange;
                lastchange = sample_num;
                if (debug) std::cerr << "Tick: " << ticklength << std::endl;
                foundbit(1);
            }
            break;
        case(CletusState::INMESSAGE):
            // We're in a message. Read half a ticklength and note the 
            // current level.
            if ((sample_num - lastchange) >= (ticklength / 2))  {
                lastlevel = level;
                state = CletusState::NEEDCHANGE;
            }
            break;
        case(CletusState::NEEDCHANGE):
            // Look for a state change.
            // When we get the state change record the bit.
            // if we don't get a change in 2 ticklengths
            // then abandon ship.
            if ((sample_num - lastchange) > (ticklength * 2)) {
                state = CletusState::ABORT;
            } else if (lastlevel != level) {
                foundbit(level ? 1 : 0);
                lastchange = sample_num;
                state = CletusState::TICKWAIT;
            }
            break;
        case(CletusState::TICKWAIT):
            // Wait 1.5 tick lengths
            if ((sample_num - lastchange) >= (ticklength * 1.5)) {
                lastlevel = level;
                state = CletusState::NEEDCHANGE;
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
    std::cerr << "cletus - simple I/Q Manchester decoding tool" << std::endl;
    std::cerr << "\te.g. rtl_sdr -g 10 -f 344900000 -s 2000000 - | ./cletus" << std::endl;
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
        // int sig = sqrt(pow(buf[0] - 127,2) + pow(buf[1] - 127,2));
        // Use I as is, ignoring Q. Very rough but also
        // very fast and fine for this task:
        int sig = abs(buf[0] - 127);

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
