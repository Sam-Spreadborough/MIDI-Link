#include <iostream>
#include <map>
#include <string>

#include "ableton/Link.hpp"
#include "RtMidi.h"

class SpinLock
{
public:
    void lock()
    {
        while(_flag.test_and_set(std::memory_order_acquire)) { }
    }

    bool try_lock()
    {
        return !_flag.test_and_set(std::memory_order_acquire);
    }

    void unlock()
    {
        _flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag _flag = ATOMIC_FLAG_INIT;
};

class AbletonMidiClock
{
public:
    AbletonMidiClock()
        : _link(120.0)
    {
        _link.enable(true);
        _link.enableStartStopSync(false);
        _link.setTempoCallback([this](double newTempo){ tempoChanged(newTempo); });
        _link.setNumPeersCallback([this](int numPeers){ numPeersChanged(numPeers); });
        _link.setStartStopCallback([this](bool startStopSync){ startStopChanged(startStopSync); });

        checkForMidiDevices();

        std::cout << "Ableton MIDI Clock started" << std::endl;
    }

    ~AbletonMidiClock()
    {
        stopThread();
        std::cout << "Ableton MIDI Clock stopped" << std::endl;
    }

    int run()
    {
        try
        {
            startThread();
            while(_running)
            {
                checkForMidiDevices();
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            return 0;
        }
        catch(const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }

private:
    void tempoChanged(double newTempo)
    {
        std::cout << "Tempo changed: " << newTempo << std::endl;
    }

    void numPeersChanged(int numPeers)
    {
        std::cout << "Num peers changed: " << numPeers << std::endl;
    }

    void startStopChanged(bool startStopSync)
    {
        if(startStopSync)
        {
            sendMidiClockStart();
        }
        else
        {
            sendMidiClockStop();
        }
    }

    void checkForMidiDevices()
    {
        RtMidiOut out;
        for(auto i = 0; i < out.getPortCount(); ++i)
        {
            auto it = _midiOutputs.find(out.getPortName(i));
            if(it == _midiOutputs.end())
            {
                std::lock_guard lockGuard(_lock);
                std::unique_ptr<RtMidiOut> output = std::make_unique<RtMidiOut>();
                output->openPort(i);
                _midiOutputs[out.getPortName(i)] = std::move(output);
                std::cout << "Opened port: " << out.getPortName(i) << std::endl;
            }
        }
    }

    void runThread()
    {
        while(_running)
        {
            static constexpr double kClockInterval{1.0 / 24.0};

            const auto state = _link.captureAudioSessionState();
            const auto phase = state.phaseAtTime(_link.clock().micros(), 1.0);
            const auto newClockIndex = static_cast<int>(std::floor(phase / kClockInterval));

            if(newClockIndex != _clockIndex)
            {
                _clockIndex = newClockIndex;
                sendMidiClock();
            }
        }
    }

    void startThread()
    {
        _thread = std::thread([this]{ runThread(); });
    }

    void stopThread()
    {
        _running = false;
        if(_thread.joinable())
        {
            _thread.join();
        }
    }

    void sendMidiClock()
    {
        static constexpr std::array<uint8_t, 1> kMidiClock {0xF8};

        if(std::unique_lock unique_Lock(_lock, std::try_to_lock); unique_Lock.owns_lock())
        {
            for(auto& [_, output] : _midiOutputs)
            {
                output->sendMessage(kMidiClock.data(), kMidiClock.size());
            }
            _lock.unlock();
        }
    }

    void sendMidiClockStart()
    {
        static constexpr std::array<uint8_t, 1> kMidiClockStart {0xFA};

        if(std::unique_lock unique_Lock(_lock, std::try_to_lock); unique_Lock.owns_lock())
        {
            for(auto& [_, output] : _midiOutputs)
            {
                output->sendMessage(kMidiClockStart.data(), kMidiClockStart.size());
            }
            _lock.unlock();
        }
    }

    void sendMidiClockStop()
    {
        static constexpr std::array<uint8_t, 1> kMidiClockStop {0xFC};

        if(std::unique_lock unique_Lock(_lock, std::try_to_lock); unique_Lock.owns_lock())
        {
            for(auto& [_, output] : _midiOutputs)
            {
                output->sendMessage(kMidiClockStop.data(), kMidiClockStop.size());
            }
            _lock.unlock();
        }
    }

    ableton::Link _link;
    std::thread _thread;
    std::atomic<bool> _running{true};
    std::unordered_map<std::string, std::unique_ptr<RtMidiOut>> _midiOutputs;
    int _clockIndex{-1};
    SpinLock _lock;
};

int main()
{
    return AbletonMidiClock().run();
}
