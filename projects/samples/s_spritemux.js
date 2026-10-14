//*****************************************************************************
// PROJECT CONFIG OVERWRITE
//*****************************************************************************

//-- Target MSX machine version (string)
Machine = "1";

//-- List of library modules to build (array)
LibModules = [ "sprite_mux", ...LibModules ];

//-- Application ID. Can be 2 character string or 16-bits integer (0~65535)
AppID = "SM";

// OpenMSX
Emulator = "/opt/homebrew/bin/openmsx"
EmulExtraParam= `-script ${ToolsDir}script/openMSX/debugger_pvm.tcl`;
DoRun = true;