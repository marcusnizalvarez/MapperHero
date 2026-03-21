// --------------------- INCLUDES ---------------------
// CPP STD libs
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <map>
#include <utility>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <variant>
#include <algorithm>
#include <mutex>
#include <atomic>

// Windows libs
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <winnt.h>
#include <winscard.h>
// NOTE: Code bellow is intended for VisualStudio?

// --------------------- DEFINITIONS ---------------------
// MIDI STATUS CONSTANTS
#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_STATUS_MASK    0xF0
#define MAIN_SLEEP_MS       250

// MIDI_NOTE → {Key, VelocityThreshold, etc...}
class NoteMetadata{
    public:
    NoteMetadata(WORD Key,unsigned char VelocityThreshold,uint64_t OverhitThreshold){
        this->Key=Key;
        this->VelocityThreshold=VelocityThreshold;
        this->OverhitThreshold=OverhitThreshold;
    };
    WORD Key;                           // Windows virtual key
    unsigned char VelocityThreshold;    // Minimum velocity required
    uint64_t      OverhitThreshold;     // Overhit prevention in ms
    // ------------------------ ATOMIC vars ------------------------
    std::atomic<uint64_t> atomic_LastHit = 0;             // Used for Overhit calculation
    std::atomic<size_t>   atomic_NHits  = 0;              // Stats
    std::atomic<size_t>   atomic_VelocityCumSum  = 0;     // Stats
    std::atomic<size_t>   atomic_NBlockedByVelocity  = 0; // Stats
    std::atomic<size_t>   atomic_NBlockedByOverhit  = 0;  // Stats
};
typedef unsigned char MIDI_NOTE_VALUE;
typedef std::map<MIDI_NOTE_VALUE, NoteMetadata> NoteMetadataMap;   //<MIDI_NOTE  ,NoteMetadata   >
//typedef std::map<MIDI_NOTE_VALUE, std::mutex>   NoteMutexMap;      //<MIDI_NOTE  ,Mutex   >

// MISC
#define RedText    "\x1b[31m"
#define GreenText  "\x1b[32m"
#define YellowText "\x1b[33m"
#define BlueText   "\x1b[34m"
#define WhiteText  "\x1b[0m"

// --------------------- CLASSES ---------------------
class LoggingClass {
public:
    const size_t HistorySize = 15;
    LoggingClass(){
        isLoggingEnabled=false;
        LogHistory=std::vector<std::string>(HistorySize,"");
    };
    ~LoggingClass(){
    };
    void PushMsgToLogHistory(std::string&& Msg){
        std::lock_guard<std::mutex> Mutex(LogHistoryMutex);
        LogHistory.erase(LogHistory.begin());
        LogHistory.push_back(std::move(Msg));
        return;
    }
    std::vector<std::string> GetHistoryMsgs(){
        std::lock_guard<std::mutex> Mutex(LogHistoryMutex);
        return LogHistory;
    }
    std::atomic<bool> isLoggingEnabled = true;
private:
    std::vector<std::string> LogHistory;
    std::mutex LogHistoryMutex;
};

inline uint64_t GetTimeInMs(){
    auto Now = std::chrono::steady_clock::now();
    auto Ms = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch());
    return Ms.count();
}

// Check if IsValueBetween range
template<typename T1> bool isValueBetween(const T1& X,const T1& Min,const T1& Max){
    return (X>=Min && X<=Max);
}
