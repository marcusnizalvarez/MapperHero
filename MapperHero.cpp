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
// Windows libs
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <winnt.h>
#include <winscard.h>
// NOTE: Code bellow is intended for VisualStudio?
// #pragma comment(lib,"winmm.lib")

// --------------------- DEFINITIONS ---------------------
// MIDI STATUS CONSTANTS
#define MIDI_NOTE_OFF       0x80
#define MIDI_NOTE_ON        0x90
#define MIDI_STATUS_MASK    0xF0
#define MAIN_SLEEP_MS       250

// MIDI_NOTE → {Key, VelocityThreshold, etc...}
typedef struct {
    WORD Key;                          // Windows virtual key
    unsigned char VelocityThreshold;   // Minimum velocity required
    uint64_t OverhitThreshold;         // Overhit prevention in ms
                                       // ------------------------
    uint64_t LastHit;                  // Used for Overhit calculation
    size_t   n_Hits;                   // Stats
    size_t   VelocityCumSum;           // Stats
    size_t   n_Blocked_byVelocity;     // Stats
    size_t   n_Blocked_byOverhit;      // Stats
} NoteMetadata;
typedef std::map<unsigned char, NoteMetadata> NoteMetadataMap;   //<MIDI_NOTE  ,NoteMetadata   >
typedef std::map<std::string, NoteMetadataMap> DeviceMappingMap; //<DEVICE_NAME,NoteMetadataMap>

// --------------------- VARIABLES ---------------------
DeviceMappingMap Mappings;
std::vector<std::string> DeviceNames;
std::vector<std::string> LogHistory(15,"");
bool isLoggingEnabled=false;
std::vector<HMIDIIN> OpenDevices;
// ---- Special Command Vars
// TODO: Remove or implement it;
// bool isHihatPedalPressed=false;
// int  SpecialCombinationCounter=0;

// --------------------- FUNCTIONS ---------------------
uint64_t GetTimeInMs(){
    auto Now = std::chrono::steady_clock::now();
    auto Ms = std::chrono::duration_cast<std::chrono::milliseconds>(Now.time_since_epoch());
    return Ms.count();
}

// Check if IsValueBetween range
template<typename T1> bool isValueBetween(const T1& X,const T1& Min,const T1& Max){
    return (X>=Min && X<=Max);
}

// Load mappings parameters from file
bool LoadMappings(const std::string &filename) {
    std::ifstream file(filename);
    if (!file) return false;
    std::string line;
    std::string DeviceName="";
    while (std::getline(file, line)) {
        // Skip comments
        {
            auto it=line.find_first_of("#");
            if(it!=std::variant_npos){
                if(it!=0){
                    printf("[LOG] Invalid mappings.txt format\n");
                    return false;
                }
                continue;
            }
        }
        // Get device name if found '[' char
        if(line.find_first_of("[") != std::variant_npos) {
            auto it1=line.find_first_of("[");
            auto it2=line.find_first_of("]");
            if(it1!=std::variant_npos && it2!=std::variant_npos){
                DeviceName=line.substr(it1 + 1,it2 - it1 - 1);
                std::cout << "[LOG] Importing mappings for " << DeviceName << "\n";
                continue;
            } else {
                printf("[LOG] Invalid mappings.txt format\n");
                return false;
            }
        }

        // Open stringstream to read from CSV
        std::istringstream iss(line);
        std::string noteStr, keyStr, velStr, ohitStr;
        if (std::getline(iss, noteStr, ',') &&
            std::getline(iss, keyStr, ',') &&
            std::getline(iss, velStr, ',') &&
            std::getline(iss, ohitStr, ',')) {
                int note = std::stoi(noteStr);
                if(!isValueBetween(note,0,127)){
                    std::cout << "[LOG] Invalid parameter in mappings.txt: Note must be between [0,127]'\n";
                    return false;
                }
                char key = keyStr[0];
                if(keyStr.length()>1){
                    // TODO: special chars in here, like VK_RETURN
                    std::cout << "[LOG] Invalid parameter in mappings.txt: Key must be single character: a,b,...,z,A,B,...,Z, ,0,1,...,9,@,# '\n";
                    return false;
                }
                int vel = std::stoi(velStr);
                if(!isValueBetween(vel,0,127)){
                    std::cout << "[LOG] Invalid parameter in mappings.txt: VelocityThreshold must be between [0,127]'\n";
                    return false;
                }
                int ohit = std::stoi(ohitStr);
                if(ohit<0 || ohit>999){
                    if(ohit < 0){
                        std::cout << "[LOG] Invalid parameter in mappings.txt: OverhitThreshold must be greater than 0'\n";
                    } else {
                        std::cout << "[LOG] Invalid parameter in mappings.txt: OverhitThreshold too high, 1000ms = 1second (are you sure?)'\n";
                    }
                    return false;
                }
                Mappings[DeviceName][static_cast<unsigned char>(note)] = {
                    static_cast<WORD>(key),
                    static_cast<unsigned char>(vel),
                    static_cast<uint64_t>(ohit),
                    GetTimeInMs(),
                    0,
                    0,
                    0,
                    0
                };
        }
    }
    return true;
}

// Print Stats Function
void PrintStats(){
    if(!isLoggingEnabled) return;
    std::cout << "\033[H";   // Move cursor to top-left (no scrolling)
    for(const auto& xDevice : Mappings){
        std::cout << " " << std::string(57,'_') << " \n";
        std::cout << "| Device: " << xDevice.first << std::string(59-10-1-xDevice.first.length(),' ') <<"|\n";
        std::cout << "| Mappings            | Hits   | Avg |      Ignored by    |\n";
        std::cout << "| Note>>Key(Vel|OHit) |        | Vel | Velocity | OverHit |\n";
        std::cout << "|_____________________|________|_____|__________|_________|\n";
        for(const auto& xMapping : xDevice.second){
            std::cout << 
             "| "         << std::setw(2) << (unsigned)xMapping.first << 
            " >> '"        << std::setw(1) << (char)xMapping.second.Key << "'" <<
            " (" << std::setw(3) << (unsigned)xMapping.second.VelocityThreshold << 
            "|" << std::setw(3) << xMapping.second.OverhitThreshold << 
            ") | "  << std::setw(6) << xMapping.second.n_Hits << 
            " | "   << std::setw(3) << (xMapping.second.n_Hits==0?0:xMapping.second.VelocityCumSum/xMapping.second.n_Hits) << 
            " | "   << std::setw(8) << xMapping.second.n_Blocked_byVelocity << 
            " | "   << std::setw(7) << xMapping.second.n_Blocked_byOverhit << 
            " |\n";
        }
        std::cout << "|_________________________________________________________|\n\n";
    }
    // --- Print last MIDI messages
    // Reminder for me: '\033[2;32H' = send cursor to row 2, col 32
    std::string TableOffset="62";
    std::cout << "\033[1;" << TableOffset << "H ______________________________";
    std::cout << "\033[2;" << TableOffset << "H|                              |";
    std::cout << "\033[3;" << TableOffset << "H|   MIDI messages              |";
    std::cout << "\033[4;" << TableOffset << "H|                              |";
    std::cout << "\033[5;" << TableOffset << "H|______________________________|";
    size_t i=LogHistory.size();
    TableOffset="63";
    for(const auto& Msg : LogHistory){
        std::cout << "\033[" << (i-- +5) << ";" << TableOffset << "H" << std::setfill(' ') << std::setw(35) << std::left << Msg;
    }
    // std::cout.flush(); No need for fast flush
}

// Callback invoked when a MIDI message arrives. bits 0–7 → status; bits 8–15  → data1 (note); bits 16–23 → data2 (velocity)
//    HMIDIIN MidiIn,       Handle to the MIDI input device that generated the message
//    UINT Msg,             MIDI input message identifier (e.g. MIM_DATA, MIM_OPEN, MIM_CLOSE)
//    DWORD_PTR Instance,   User-defined pointer/value supplied to midiInOpen() (callback context)
//    DWORD_PTR Param1,     Message-specific data (for MIM_DATA: packed MIDI message)
//    DWORD_PTR Param2      Timestamp in milliseconds when the message was received
void CALLBACK MidiCallback(HMIDIIN MidiIn,UINT Msg,DWORD_PTR Instance,DWORD_PTR Param1,DWORD_PTR Param2) {
    if(Msg!=MIM_DATA) return;

    //std::string DeviceName = *(std::string*)Instance; // Instance is passed as a pointer value, so get it's value
    unsigned char Status = Param1 & 0xFF;
    unsigned char Note   = (Param1 >> 8) & 0xFF;
    unsigned char Vel    = (Param1 >> 16) & 0xFF;
    unsigned char Type = Status & MIDI_STATUS_MASK;

    // Do nothing if message is different from (NOTE_ON + Vel>0)
    if(!(Type==MIDI_NOTE_ON && Vel>0)) {
        return;
    }

    // Set pointer to KeyMapping
    NoteMetadata* x = nullptr;
    NoteMetadataMap* NoteMap = &Mappings.at(DeviceNames.at(Instance));
    auto It = NoteMap->find(Note);
    if(It != NoteMap->end()) {
        x = &It->second;
    }
    
    // Log the MIDI note stats
    std::string LogMsg;
    if(isLoggingEnabled){
        LogMsg = "Note=" + std::to_string((int)Note) + " Vel=" + std::to_string((int)Vel);
        LogHistory.erase(LogHistory.begin());
        if(x==nullptr){
            LogMsg+=" !UNMAPPED";
            LogHistory.push_back(LogMsg);
            return;
        } 
        LogMsg+=" >> '" + std::string(1,(char)x->Key) + "'";
    }
    
    // velocity filter
    x->VelocityCumSum+=Vel;
    if(Vel < x->VelocityThreshold) {
        if(Vel > 0){
            x->n_Blocked_byVelocity++;
            if(isLoggingEnabled){
                LogMsg+=" !VLCT";
                LogHistory.push_back(LogMsg);
            }
                
        }
        return;
    }

    // Overhit filter
    uint64_t Now=GetTimeInMs();
    uint64_t Diff=(Now - x->LastHit);
    x->LastHit=Now;
    if(Diff < x->OverhitThreshold){
        x->n_Blocked_byOverhit++;
        if(isLoggingEnabled){
            LogMsg+=" !OHIT " + std::to_string(Diff) +"ms";
            LogHistory.push_back(LogMsg);
        }
        return;
    }

    // Finally, emit keyboard press
    INPUT Input={0};
    Input.type=INPUT_KEYBOARD;
    Input.ki.wVk=x->Key;
    SendInput(1,&Input,sizeof(INPUT));
    Input.ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(1,&Input,sizeof(INPUT));
    x->n_Hits++;

    // Print fast log
    if(isLoggingEnabled){
        LogHistory.push_back(LogMsg);
    }
}

// --------------------- MAIN ---------------------
// midiInOpen(): Open a MIDI input device using the WinMM API. Parameters:
//    1) &MidiDevice             → Output handle that will receive the opened MIDI device.
//    2) MIDI_DEVICE_ID          → Index of the MIDI input device (0 = first available device).
//    3) (DWORD_PTR)MidiCallback → Pointer to the callback function that will be invoked by the OS whenever a MIDI message is received.
//    4) 0                       → User-defined instance value passed back to the callback (unused here).
//    5) CALLBACK_FUNCTION       → Indicates that the callback pointer is a function.
// The function returns MMSYSERR_NOERROR on success. Any other value indicates that the device could not be opened (device missing, busy, or invalid ID).
int main() {
    // Set UTF-8 Output
    SetConsoleOutputCP(CP_UTF8);

    // Print Splash
    {
            std::cout << "━━━━━━━━━━┏━┓┏━┓━━━━━━━━━━━━━━━┓━┏┓━━━━━━━━━━━━━━━━\n";
            std::cout << "━━━━━━━━━━┃┃┗┛┃┃━━━━━━━━━━━━━━━┃━┃┃━━━━━━━━━━━━━━━━\n";
            std::cout << "━━━━━━━━━━┃┏┓┏┓┃━━┓━━━┓━━┓━━┓━┓┗━┛┃━━┓━┓━━┓━━━━━━━━\n";
            std::cout << "━━━━━━━━━━┃┃┃┃┃┃━┓┃━┏┓┃┏┓┃┏┓┃┏┛┏━┓┃┏┓┃┏┛┏┓┃━━━━━━━━\n";
            std::cout << "━━━━━━━━━━┃┃┃┃┃┃┗┛┗┓┗┛┃┗┛┃┃━┫┃━┃━┃┃┃━┫┃━┗┛┃━━━━━━━━\n";
            std::cout << "━━━━━━━━━━┗┛┗┛┗┛━━━┛┏━┛┏━┛━━┛┛━┛━┗┛━━┛┛━━━┛━━━━━━━━\n";
            std::cout << "━━━━━━━━━━━━━━━━━━━━┃━━┃━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cout << "━━━━━━━━━━━━━━━━━━━━┛━━┛━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cout << " *** A MIDI remapping tool for CloneHero game. *** \n";
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    }

    // Load mappings.txt file
    if (!LoadMappings("mappings.txt")) {
        printf("[LOG] Failed to open mappings.txt\n");
        getchar();
        return EXIT_FAILURE;
    }
    
    UINT NumDevices = midiInGetNumDevs();
    for(UINT DeviceID = 0; DeviceID < NumDevices; DeviceID++){
        MIDIINCAPS Caps;
        if(midiInGetDevCaps(DeviceID,&Caps,sizeof(Caps)) != MMSYSERR_NOERROR)
            continue;
        std::string DeviceName = Caps.szPname;
        printf("[LOG] Detected MIDI device: %s\n",DeviceName.c_str());
        
        // Check if device name exists in mappings
        if(Mappings.find(DeviceName) == Mappings.end()){
            printf("[LOG] No mappings for %s, skipping\n",DeviceName.c_str());
            continue;
        }

        printf("[LOG] Opening mapped device: %s\n",DeviceName.c_str());
        DeviceNames.push_back(DeviceName);
        HMIDIIN MidiDevice;
        if(midiInOpen(&MidiDevice,DeviceID,(DWORD_PTR)MidiCallback,DeviceNames.size()-1,CALLBACK_FUNCTION) != MMSYSERR_NOERROR){
            printf("[LOG] Failed to open device: %s\n",DeviceName.c_str());
            DeviceName.pop_back();
            continue;
        }
        midiInStart(MidiDevice);
        OpenDevices.push_back(std::move(MidiDevice));
    }

    // Ignore configs for undetected devices
    {
        std::vector<std::string> UndetectedDevices;
        for(const auto& x : Mappings){
            if(std::find(DeviceNames.begin(),DeviceNames.end(),x.first)==DeviceNames.end()){
                UndetectedDevices.push_back(x.first);
                std::cout<<"[LOG] "<<x.first<<" device not found, ignoring config from mappings.txt\n";
            }
        }
        for(const auto& x : UndetectedDevices) Mappings.erase(x);
    }
    
    if(OpenDevices.empty()){
        printf("[LOG] No mapped MIDI devices found\n");
        getchar();
        return 0;
    }

    // Wait before Print stats
    std::cout << "[LOG] The program is already running...\n";
    std::cout << "PRESS 'ENTER' TO VIEW STATISTICS (OPITIONAL)\n";
    getchar();
    isLoggingEnabled=true;
    
    // Main loop
    std::cout << "\033[2J";   // Clear Screen
    std::cout << "\033[?25l"; // Hide Cursor
    while(true){
        PrintStats();
        Sleep(MAIN_SLEEP_MS);
    }
    std::cout << "\033[?25h"; // Show cursor

    // Cleanup
    for(HMIDIIN Dev : OpenDevices){
        midiInStop(Dev);
        midiInClose(Dev);
    }
    getchar();
    return 0;
}