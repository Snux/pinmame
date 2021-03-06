#if defined(PINMAME) && defined(PROC_SUPPORT)

#include <stdarg.h>

extern "C" {
#include "driver.h"
#include "wpc/core.h"
#include "wpc/sim.h"
}

#include <fstream>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <p-roc/pinproc.h>
#include "p-roc.h"
#include "p-roc_drivers.h"

// Handle to the P-ROC instance.
PRHandle proc = NULL;

// Global machineType is used in many P-ROC functions to do different things
// based on the type of machine being used.
PRMachineType machineType;

// Create a global yamlDoc to hold the machine data parsed from the YAML file.
// Other p-roc support files need access to it. No sense passing it around
// everywhere.
YAML::Node yamlDoc;

// We configure the position of the S11 ball and credit displays from the
// YAML file.  Rather than read the YAMLDOC for every segment drawn to get
// the info, create globals for them.  Less CPU cycles.
// doubleAlpha indicates whether this machine has a lower display of 16
// alphanumeric positions
int S11CreditPos=0;
int S11BallPos=0;
int doubleAlpha=0;
int startButtonHoldTime=0;

// BOP emulation cannot handle the timing for the helmet lamps
// This option will force a generic twinkle effect via the aux port
// whenever the GI is lit
bool BOPTwinkle = false;
int twinkleTime = 0;
int motorOnTime = 0;
int motorOffTime = 0;
int helmetPatterns[1000];
int twinkleCount = 0;
bool isHelmetFile = false;

// Determine whether automatic patter detection is on or off by default
bool autoPatterDetection = true;

// Is there an arduino connected for RGB effects?
bool isArduino = false;
char arduinoPort[] = "NONE";


// Load/Parse the YAML file.
PRMachineType procLoadMachineYAML(char *filename) {
	try	{
		std::ifstream fin(filename);
		if (fin.is_open() == false) {
			fprintf(stderr, "YAML file not found: %s\n", filename);
			return kPRMachineInvalid;
		}
		YAML::Parser parser(fin);

		while(parser) {
				parser.GetNextDocument(yamlDoc);
		}
                
		std::string machineTypeString;
		yamlDoc["PRGame"]["machineType"] >> machineTypeString;
		if (machineTypeString == "wpc") {
			machineType = kPRMachineWPC;
			fprintf(stderr, "YAML machine type: kPRMachineWPC\n");
		} else if (machineTypeString == "wpc95") {
			machineType = kPRMachineWPC95;
			fprintf(stderr, "YAML machine type: kPRMachineWPC95\n");
		} else if (machineTypeString == "wpcAlphanumeric") {
			machineType = kPRMachineWPCAlphanumeric;
			fprintf(stderr, "YAML machine type: kPRMachineWPCAlphanumeric\n");
		} else if(machineTypeString == "sternWhitestar") {
			machineType = kPRMachineSternWhitestar;
			fprintf(stderr, "YAML machine type: kPRMachineSternWhitestar\n");
		} else if(machineTypeString == "sternSAM") {
			machineType = kPRMachineSternSAM;
			fprintf(stderr, "YAML machine type: kPRMachineSternSAM\n");
		} else {
			fprintf(stderr, "Unknown machine type in YAML file: %s\n", machineTypeString.c_str());
			return kPRMachineInvalid;
		}
	}
	catch (...)	{
		fprintf(stderr, "Unexpected exception while parsing YAML config.\n");
	}

	return machineType;
}

// Set the machine type.
PRMachineType getRomMachineType() {
	// First set the machine type based on the ROM being run.
	switch (core_gameData->gen) {
		case GEN_WPCALPHA_1:
		case GEN_WPCALPHA_2:
			if (pmoptions.alpha_on_dmd) {
				return kPRMachineWPC;
				fprintf(stderr, "ROM machine type: kPRMachineWPCAlphanumeric,\nbut using kPRMachineWPC due to alpha_on_dmd option\n");
			} else {
				return kPRMachineWPCAlphanumeric;
				fprintf(stderr, "ROM machine type: kPRMachineWPCAlphanumeric\n");
			}
			break;
                case GEN_DE:
                
                case GEN_S11A:
                    
                case GEN_S11:
                case GEN_S11B2:
                case GEN_S11C:

			if (pmoptions.alpha_on_dmd) {
				return kPRMachineWPC;
				fprintf(stderr, "ROM machine type: kPRMachineWPCAlphanumeric,\nbut using kPRMachineWPC due to alpha_on_dmd option\n");
			} else {
				return kPRMachineWPCAlphanumeric;
                                //return kPRMachineWPC;
                                fprintf(stderr, "ROM machine type: kPRMachineWPCAlphanumeric\n");
			}
			break;
                case GEN_DEDMD32:
                	return kPRMachineWPC;
			fprintf(stderr, "ROM machine type: kPRMachineWPC\n");
			break;
		case GEN_WPCDMD:
		case GEN_WPCFLIPTRON:
		case GEN_WPCDCS:
		case GEN_WPCSECURITY:
			return kPRMachineWPC;
			fprintf(stderr, "ROM machine type: kPRMachineWPC\n");
			break;
		case GEN_WPC95DCS:
		case GEN_WPC95:
			return kPRMachineWPC95;
			fprintf(stderr, "ROM machine type: kPRMachineWPC95\n");
			break;
		case GEN_WS:
		case GEN_WS_1:
		case GEN_WS_2:
			return kPRMachineSternWhitestar;
			fprintf(stderr, "ROM machine type: kPRMachineSternWhitestar\n");
			break;
		case GEN_SAM:
			return kPRMachineSternSAM;
			fprintf(stderr, "ROM machine type kPRMachineSternSAM\n");
			break;
		default:
			return kPRMachineInvalid;
			fprintf(stderr, "Unknown ROM machine type in YAML file\n");
	}
}

void setMachineType(char *yaml_filename) {
	if (strcmp(yaml_filename, "None") == 0) {
		machineType = kPRMachineInvalid;
	} else {
		std::ifstream fin(yaml_filename);
		if (fin.is_open() == false) {
			fprintf(stderr, "YAML file not found: %s\n", yaml_filename);
			machineType = kPRMachineInvalid;
		} else {
			// TODO: Make sure the machineType field exists in file
			machineType = procLoadMachineYAML(yaml_filename);
		}
	}
}

// Send all pending commands to the P-ROC.
void procFlush(void) {
	PRFlushWriteData(proc);
}

void procDeinitialize() {
	if (proc) {
		PRDelete(proc);
	}
}

std::string procGetYamlPinmameSettingString(const char *key, const char *defaultValue) {
    std::string retval;

    try {
       yamlDoc["PRPinmame"][key] >> retval;
    }
    catch (...) {
        retval = defaultValue;
    }

    return retval;
}

int procGetYamlPinmameSettingInt(const char *key, int defaultValue) {
    int retval;

    try {
       yamlDoc["PRPinmame"][key] >> retval;
    }
    catch (...) {
        // Not defined in YAML or not numeric
        retval = defaultValue;
    }
    
    return retval;
}

// When testing and using pinmame it is useful to be able to run
// the diagnostic switches from the keyboard instead of the machine
// as having the door switches work the P-ROC requires additional
// cabling. A YAML entry controls whether the keyboard is active or not
int procKeyboardWanted(void) {
    return (procGetYamlPinmameSettingString("keyboard", "off") == "on");
}

// To quit pinmame, you can hold both flippers and press the start button at the same time.
// For machines that do not register flipper buttons when the flippers themselves are disabled
// we can use a parameter in the YAML to determine a period of time to just hold the start button
void procCheckQuitMethod(void) {
    startButtonHoldTime = procGetYamlPinmameSettingInt("startButtonHoldTime", 0);
    if (startButtonHoldTime > 0) printf("\nStart button hold time to quit : %dms\n",startButtonHoldTime);
    else printf("\nHolding start button will not quit");
    
    }


// Check patter detection from YAML
void setPatterDetection(void) {
    autoPatterDetection = (procGetYamlPinmameSettingString("autoPatterDetection", "on") == "on");
    
    printf("\nAutomatic patter detection : %s\n", autoPatterDetection ? "Enabled" : "Disabled");
    
}

// Check if we have an arduino port specified within the YAML which will be used for 
// controlling the RGB lamps
void procCheckArduinoRGB(void) {
    std::string port;

    try {
        yamlDoc["PRGame"]["arduino"] >> port;
    }
    catch (...) {
        port = "none";
    }
    if (port == "none") 
        isArduino = false;
    else {
        isArduino = true;
        strcpy(arduinoPort, port.c_str());
        procConfigureRGBLamps();
        printf("\nArduino enabled for RGB control on port %s",arduinoPort);
    }
}

// Bride of Pinbot can have some additional handling.  The clock/data lines which control the helmet lamps need
// more precise timing than we can catch via emulation.  By default, the code will still try to run with
// emulation, but an alternative is to add a BOPTwinkle parameter into the YAML file.  In this case, the helmet lamps
// (and the motor/relay, since they are in the same driver group) will be controlled via the aux command logic
void procSetBOPHelmet(void) {
    std::string helmetfile;
    twinkleTime = procGetYamlPinmameSettingInt("BOPTwinkle", 0);
    if (twinkleTime > 0) {
        // If the twinkleTime (time between lamp transitions) exists in the YAML, look to see if a file of binary
        // lamp patterns is also defined.
        helmetfile = procGetYamlPinmameSettingString("BOPHelmetFile", "none");
        if (helmetfile != "none")
            isHelmetFile = true;
        motorOnTime = procGetYamlPinmameSettingInt("BOPMotorOn", 65);
        motorOffTime = procGetYamlPinmameSettingInt("BOPMotorOff", 65);
        
        // If there is a file of transitions defined, then read it in.
        if (isHelmetFile) {
            if (mame_debug) fprintf(stderr,"\n Helmet lamp file is %s",helmetfile.c_str());
              std::string line;
              std::ifstream myfile(helmetfile.c_str());
              if (myfile.is_open())
              {
                while ( getline (myfile,line) )
                {
                    char * ptr;
                    long parsed = strtol(line.c_str(), & ptr, 2);
                    if (mame_debug) fprintf(stderr,"\n Read %lX\n", parsed);
                    helmetPatterns[twinkleCount++] = parsed;
                }
                myfile.close();
              }
        }

        BOPTwinkle = true;

        // Driver group 8 is the one we need to control precisely so disable it from being handled via
        // standard P-ROC control
        PRDriverGroupDisable(proc,8);
        //setupBOPAuxCommands();
        // And then disable them from a pinmame automatic handling perspective
        AddIgnoreCoil(64); // Helmet Data Port
        AddIgnoreCoil(65); // Helmet Clock Port
        AddIgnoreCoil(66); // Motor Relay
        AddIgnoreCoil(67); // Head Motor
        if (mame_debug) fprintf(stderr,"\n\nBride of Pinbot Aux enabled with twinkleTime %d, motor on %d, motor off %d\n",twinkleTime,motorOnTime,motorOffTime);
    }
    else BOPTwinkle = false;
    
}


// Called to set the credit/ball display positions
void procBallCreditDisplay(void) {
    S11CreditPos = procGetYamlPinmameSettingInt("s11CreditDisplay", 0);
    doubleAlpha = procGetYamlPinmameSettingInt("doubleAlpha", 0);
    S11BallPos = procGetYamlPinmameSettingInt("s11BallDisplay", 0);
}

// Clear the contents of the P-ROC aux memory to ensure it doesn't try to
// execute random instructions on startup.
void procClearAuxMemory(void) {
	int cmd_index=0;
	PRDriverAuxCommand auxCommands[255];
	
	if (proc) {
		PRDriverAuxPrepareDisable(&auxCommands[cmd_index++]);
		while (cmd_index < 255) {
			PRDriverAuxPrepareJump(&auxCommands[cmd_index++], 0);
		}
		// Send the commands.
		PRDriverAuxSendCommands(proc, auxCommands, cmd_index, 0);
		procFlush();
	}
}

// Initialize the P-ROC hardware.
int procInitialize(char *yaml_filename) {
	fprintf(stderr, "\n\n****** Initializing P-ROC ******\n");
        setMachineType(yaml_filename);
        setPatterDetection();
        procCheckQuitMethod();
        
	if (machineType != kPRMachineInvalid) {
		proc = PRCreate(machineType);
		if (proc == kPRHandleInvalid) {
			fprintf(stderr, "Error during PRCreate: %s\n", PRGetLastErrorText());
			fprintf(stderr, "\n****** Ending P-ROC Initialization ******\n");
			return 0;
		} else {
			PRReset(proc, kPRResetFlagUpdateDevice);

			procClearAuxMemory();
			procConfigureDefaultSwitchRules();
			procConfigureInputMap();

			procInitializeCoilDrivers();
			procConfigureSwitchRules();

			procConfigureDriverDefaults();
                        
                        procSetBOPHelmet();
                        procCheckArduinoRGB();

			if (machineType != kPRMachineWPCAlphanumeric) {
				procDMDInit();
			}
                        fprintf(stderr, "\n****** P-ROC Initialization COMPLETE ******\n\n");
		}
	}

	if (machineType != kPRMachineInvalid) {
		return 1;
	} else {
		return 0;
	}
}

int procIsActive(void) {

	PRMachineType romMachineType = getRomMachineType();

	// Now compare machine types. If not the same, there's
	// a problem.
	if (proc) {
		if (machineType != romMachineType) {
			fprintf(stderr, "Machine type in YAML does not match the machine type of the ROM.\n");
			return 0;
		} else {
			procSetSwitchStates();
			return 1;
		}
	} else {
		return 0;
	}
}

// Tickle the P-ROC's watchdog so it doesn't disable driver outputs.
void procTickleWatchdog(void) {
	PRDriverWatchdogTickle(proc);
}

// The following is a work around for using MinGW with gcc 3.2.3 to compile
// the yaml-cpp dependency. gcc 3.2.3 is missing the definition of 'strtold'
// in libstdc++, and yaml-cpp makes heavy use of stringstream, which uses
// strtold internally. Defining strtold here allows pinmame to link with
// yaml-cpp, and by using strtod, it will work properly for everything except
// longs, which shouldn't be used in pinball YAML files anyway.

#if (__MINGW32__) && (__GNUC__) && (__GNUC__ < 4)
long double strtold(const char *__restrict__ nptr, char **__restrict__ endptr) {
	return strtod(nptr, endptr);
}
#endif

#endif /* PINMAME && PROC_SUPPORT */
