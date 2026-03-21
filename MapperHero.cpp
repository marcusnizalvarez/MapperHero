#include "MapperHero.h"
#include <winscard.h>

// --------------------- VARIABLES ---------------------
std::map<std::string, NoteMetadataMap> Mappings;
std::mutex MappingsMutex;
std::vector<std::string> DeviceNames;
std::vector<HMIDIIN> OpenMidiDevices;
LoggingClass Logging;

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
                    std::cout << RedText << "[LOG] Invalid mappings.txt format\n" << WhiteText;
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
                std::cout << RedText << "[LOG] Invalid mappings.txt format\n" << WhiteText;
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
                    std::cout << RedText << "[LOG] Invalid parameter in mappings.txt: Note must be between [0,127]'\n" << WhiteText;
                    return false;
                }
                char key = keyStr[0];
                if(keyStr.length()>1){
                    // TODO: special chars in here, like VK_RETURN
                    std::cout << RedText << "[LOG] Invalid parameter in mappings.txt: Key must be single character: a,b,...,z,A,B,...,Z, ,0,1,...,9,@,# '\n" << WhiteText;
                    return false;
                }
                int vel = std::stoi(velStr);
                if(!isValueBetween(vel,0,127)){
                    std::cout << RedText << "[LOG] Invalid parameter in mappings.txt: VelocityThreshold must be between [0,127]'\n" << WhiteText;
                    return false;
                }
                int ohit = std::stoi(ohitStr);
                if(ohit<0 || ohit>999){
                    if(ohit < 0){
                        std::cout << RedText << "[LOG] Invalid parameter in mappings.txt: OverhitThreshold must be greater than 0'\n" << WhiteText;
                    } else {
                        std::cout << RedText << "[LOG] Invalid parameter in mappings.txt: OverhitThreshold too high, 1000ms = 1second (are you sure?)'\n" << WhiteText;
                    }
                    return false;
                }
                Mappings[DeviceName].try_emplace(
                    static_cast<MIDI_NOTE_VALUE>(note),
                    static_cast<WORD>(key),
                    static_cast<unsigned char>(vel),
                    static_cast<uint64_t>(ohit)
                );
                
                //Mappings[DeviceName].emplace(
                //    std::piecewise_construct,
                //    std::forward_as_tuple(static_cast<MIDI_NOTE_VALUE>(note)),
                //    std::forward_as_tuple(NoteMetadata(static_cast<WORD>(key),static_cast<unsigned char>(vel),static_cast<uint64_t>(ohit)))
                //);
                //NoteMetadata x(static_cast<WORD>(key),static_cast<unsigned char>(vel),static_cast<uint64_t>(ohit));

                //Mappings[DeviceName][static_cast<MIDI_NOTE_VALUE>(note)] = {
                //    static_cast<WORD>(key),
                //    static_cast<unsigned char>(vel),
                //    static_cast<uint64_t>(ohit)
                //};
        }
    }
    return true;
}

// Print Stats Function
void PrintStats(){
    std::cout << "\033[H";   // Move cursor to top-left (no scrolling)
    for(const auto& xDevice : Mappings){
        std::cout << " " << std::string(57,'_') << " \n";
        std::cout << "| Device: " << xDevice.first << std::string(59-10-1-xDevice.first.length(),' ') <<"|\n";
        std::cout << "| Mappings            | Hits   | Avg |      Ignored by    |\n";
        std::cout << "| Note>>Key(Vel|OHit) |        | Vel | Velocity | OverHit |\n";
        std::cout << "|_____________________|________|_____|__________|_________|\n";
        for(const auto& rNoteMetadata : xDevice.second){
            std::cout << 
             "| "  << std::setw(2) << (unsigned)rNoteMetadata.first << 
            " >> '"<< std::setw(1) << (char)rNoteMetadata.second.Key << "'" <<
            " ("   << std::setw(3) << (unsigned)rNoteMetadata.second.VelocityThreshold << 
            "|"    << std::setw(3) << rNoteMetadata.second.OverhitThreshold << 
            ") | " << std::setw(6) << rNoteMetadata.second.atomic_NHits.load() << 
            " | "  << std::setw(3) << (rNoteMetadata.second.atomic_NHits.load()==0?0:rNoteMetadata.second.atomic_VelocityCumSum.load()/rNoteMetadata.second.atomic_NHits.load()) << 
            " | "  << std::setw(8) << rNoteMetadata.second.atomic_NBlockedByVelocity.load() << 
            " | "  << std::setw(7) << rNoteMetadata.second.atomic_NBlockedByOverhit.load() << 
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
    
    size_t i=Logging.HistorySize;
    TableOffset="63";
    for(const auto& Msg : Logging.GetHistoryMsgs()){
        std::cout << "\033[" << (i-- +5) << ";" << TableOffset << "H" << std::setfill(' ') << std::setw(35) << std::left << Msg;
    }
    std::cout << std::endl;
}

// Callback invoked when a MIDI message arrives. bits 0–7 → status; bits 8–15  → data1 (note); bits 16–23 → data2 (velocity)
//    HMIDIIN MidiIn,       Handle to the MIDI input device that generated the message
//    UINT Msg,             MIDI input message identifier (e.g. MIM_DATA, MIM_OPEN, MIM_CLOSE)
//    DWORD_PTR Instance,   User-defined pointer/value supplied to midiInOpen() (callback context)
//    DWORD_PTR Param1,     Message-specific data (for MIM_DATA: packed MIDI message)
//    DWORD_PTR Param2      Timestamp in milliseconds when the message was received
void CALLBACK MidiCallback(HMIDIIN MidiIn,UINT Msg,DWORD_PTR Instance,DWORD_PTR Param1,DWORD_PTR Param2) {
    if(Msg!=MIM_DATA) return;

    unsigned char   Status = Param1 & 0xFF;
    MIDI_NOTE_VALUE Note   = (Param1 >> 8) & 0xFF;
    unsigned char   Vel    = (Param1 >> 16) & 0xFF;
    unsigned char   Type = Status & MIDI_STATUS_MASK;

    // Do nothing if message is different from (NOTE_ON + Vel>0)
    if(!(Type==MIDI_NOTE_ON && Vel>0)) return;

    // Seek for DeviceName comparing HMIDIIN to each OpenMidiDevices
    std::string DeviceName="";
    for(size_t i=0;i<OpenMidiDevices.size();i++){
        if(OpenMidiDevices.at(i)==MidiIn){
            DeviceName=DeviceNames.at(i);
            break;
        }
    }
    if(DeviceName=="") return;
    
    // Set pointer to KeyMapping
    NoteMetadataMap* NoteMap = &Mappings.at(DeviceName);
    auto It = NoteMap->find(Note);
    NoteMetadata* pNoteMetadata = nullptr;
    if(It != NoteMap->end()) {
        pNoteMetadata = &It->second;
    }

    // Log the MIDI note stats
    std::string LogMsg = "Note=" + std::to_string((int)Note) + " Vel=" + std::to_string((int)Vel);

    bool tmp_isLogginEnabled=Logging.isLoggingEnabled.load();
    // -- IF UNMAPPED,
    if(pNoteMetadata==nullptr){
        if(tmp_isLogginEnabled){
            Logging.PushMsgToLogHistory(
                LogMsg + " !UNMAPPED"
            );
        }
        return;
    }
    // -- ELSE, IS MAPPED:
    if(tmp_isLogginEnabled){
        LogMsg+=" >> '" + std::string(1,(char)pNoteMetadata->Key) + "'";
    }
    
    // ---------------- MUTEX LOCKED !!! ----------------
    //std::lock_guard<std::mutex> Mutex(MappingsMutex);
    
    // velocity filter
    if(tmp_isLogginEnabled){
        //pNoteMetadata->VelocityCumSum+=Vel;
        pNoteMetadata->atomic_VelocityCumSum.fetch_add(1);
    }
    if(Vel < pNoteMetadata->VelocityThreshold) {
        if(Vel > 0){
            if(tmp_isLogginEnabled){
                //pNoteMetadata->n_Blocked_byVelocity++;
                pNoteMetadata->atomic_NBlockedByVelocity.fetch_add(1);
                LogMsg+=" !VLCT";
                Logging.PushMsgToLogHistory(std::move(LogMsg));
            }
                
        }
        return;
    }

    // Overhit filter
    uint64_t Now=GetTimeInMs();
    uint64_t Diff=(Now - pNoteMetadata->atomic_LastHit);
    pNoteMetadata->atomic_LastHit=Now;
    if(Diff < pNoteMetadata->OverhitThreshold){
        if(tmp_isLogginEnabled){
            //pNoteMetadata->n_Blocked_byOverhit++;
            pNoteMetadata->atomic_NBlockedByOverhit.fetch_add(1);
            LogMsg+=" !OHIT " + std::to_string(Diff) +"ms";
            Logging.PushMsgToLogHistory(std::move(LogMsg));
        }
        return;
    }

    // Finally, emit keyboard press
    INPUT Input={0};
    Input.type=INPUT_KEYBOARD;
    Input.ki.wVk=pNoteMetadata->Key;
    SendInput(1,&Input,sizeof(INPUT));
    Input.ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(1,&Input,sizeof(INPUT));

    // Print fast log
    if(Logging.isLoggingEnabled){
        //pNoteMetadata->n_Hits++;
        pNoteMetadata->atomic_NHits.fetch_add(1);
        Logging.PushMsgToLogHistory(std::move(LogMsg));
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
            std::cout << BlueText << " *** A MIDI remapping tool for CloneHero game. *** \n" << WhiteText;
            std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    }

    // Load mappings.txt file
    if (!LoadMappings("mappings.txt")) {
        std::cout << RedText << "[LOG] Failed to open mappings.txt\n" << WhiteText;
        getchar();
        return EXIT_FAILURE;
    }
    
    //std::vector<HMIDIIN> OpenMidiDevices;
    //std::vector<OpenMidiDevice> OpenMidiDevices;
    UINT NumDevices = midiInGetNumDevs();
    for(UINT DeviceID = 0; DeviceID < NumDevices; DeviceID++){
        MIDIINCAPS Caps;
        if(midiInGetDevCaps(DeviceID,&Caps,sizeof(Caps)) != MMSYSERR_NOERROR)
            continue;
        std::string DeviceName = Caps.szPname;
        std::cout << "[LOG] Detected MIDI device: " << DeviceName << '\n';
        
        // Check if device name exists in mappings
        if(Mappings.find(DeviceName) == Mappings.end()){
            std::cout << YellowText << "[LOG] No mappings for " << DeviceName << ", skipping\n" << WhiteText;
            continue;
        }
        
        // Open HMIDIIN devices
        std::cout << GreenText << "[LOG] Opening mapped device: " << DeviceName << '\n' << WhiteText;
        HMIDIIN MidiDevice;
        if(midiInOpen(
                &MidiDevice,
                DeviceID,
                (DWORD_PTR)MidiCallback,
                0, // WARN: Disabled, I was using: 'DeviceNames.size()-1'
                CALLBACK_FUNCTION) != MMSYSERR_NOERROR){
            std::cout << RedText << "[LOG] Failed to open device: " << DeviceName << '\n' << WhiteText;
            continue;
        }
        DeviceNames.push_back(DeviceName);
        midiInStart(MidiDevice);
        OpenMidiDevices.push_back(MidiDevice);
        //OpenMidiDevices.push_back(OpenMidiDevice{MidiDevice});
    }

    // Ignore configs for undetected devices
    {
        std::vector<std::string> UndetectedDevices;
        for(const auto& x : Mappings){
            if(std::find(DeviceNames.begin(),DeviceNames.end(),x.first)==DeviceNames.end()){
                UndetectedDevices.push_back(x.first);
                std::cout<< YellowText << "[LOG] "<< x.first << " device not found, ignoring config from mappings.txt\n" << WhiteText;
            }
        }
        for(const auto& x : UndetectedDevices) Mappings.erase(x);
    }
    
    // If No Device open, terminate
    if(OpenMidiDevices.empty()){
        std::cout << RedText << "[LOG] No mapped MIDI devices found\n" << WhiteText;
        getchar();
        return 0;
    }

    // Wait before Print stats
    std::cout << "[LOG] The program is already running...\n";
    std::cout << "PRESS 'ENTER' TO VIEW STATISTICS (OPITIONAL)\n";
    getchar();
    Logging.isLoggingEnabled.store(true);
    
    // Main loop
    std::cout << "\033[2J";   // Clear Screen
    std::cout << "\033[?25l"; // Hide Cursor
    while(true){
        PrintStats();
        Sleep(MAIN_SLEEP_MS);
    }
    std::cout << "\033[?25h"; // Show cursor
    
    // Cleanup
    for(HMIDIIN Dev : OpenMidiDevices){
        midiInStop(Dev);
        midiInClose(Dev);
    }

    getchar();
    return 0;
}