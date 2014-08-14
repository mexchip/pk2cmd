//                            Software License Agreement
//
// Copyright (c) 2005-2009, Microchip Technology Inc. All rights reserved.
// Portions of this code by Jeff Post, j_post <AT> pacbell <DOT> net
//
// You may use, copy, modify and distribute the Software for use with Microchip
// products only.  If you distribute the Software or its derivatives, the
// Software must have this entire copyright and disclaimer notice prominently
// posted in a location where end users will see it (e.g., installation program,
// program headers, About Box, etc.).  To the maximum extent permitted by law,
// this Software is distributed "AS IS" and WITHOUT ANY WARRANTY INCLUDING BUT
// NOT LIMITED TO ANY IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR
// PARTICULAR PURPOSE, or NON-INFRINGEMENT. IN NO EVENT WILL MICROCHIP OR ITS
// LICENSORS BE LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL
// DAMAGES OF ANY KIND ARISING FROM OR RELATED TO THE USE, MODIFICATION OR
// DISTRIBUTION OF THIS SOFTWARE OR ITS DERIVATIVES.
//
//---------------------------------------------------------------------------
#include <iostream>

#include "stdafx.h"
#include "stdlib.h"
#include "cmd_app.h"
#include "string.h"
#include "time.h"
#include    <termios.h>
#include    <sys/ioctl.h>
extern bool verbose;

extern "C"{
	#include "strnatcmp.h"
}

#define DEVICE_FILE_NAME "PK2DeviceFile.dat"
#define DEVICE_FILE_NAME_LENGTH 17

#define NSIZE(v) (int)(v.size())

Ccmd_app::Ccmd_app(void)
{
	preserveEEPROM = false;
	hexLoaded = false;
	usingLowVoltageErase = false;
	resetOnExit = false;
	Pk2Operation = true;
	pk2UnitIndex = 0;		
	ReturnCode = OPSUCCESS;
}

Ccmd_app::~Ccmd_app(void)
{
}

void Ccmd_app::PK2_CMD_Entry(TextVec& args)
{
	_TCHAR tempString[MAX_PATH] = "";

	processArgvForSpaces(args);
	args = nargs;

	// Check for help display requests
	if (checkHelp1(args))
	{
		return;
	}

	// Load device file
	bool loadDeviceFileFailed = false;
	if (checkDevFilePathOptionB(args, tempString))
	{ // check for explicit path with -B
		if (tempString[TXT_LENGTH(tempString)-1] != '/')
			TXT_PUSH_UNSAFE(tempString, "/" DEVICE_FILE_NAME);
		else
			TXT_PUSH_UNSAFE(tempString, DEVICE_FILE_NAME);
	}
	else if (ReturnCode == INVALID_CMDLINE_ARG)
	{ // -B, but bad path
		return;
	}
	else
	{ // no -B, check PATH
		_tsearchenv_s(DEVICE_FILE_NAME, "PATH", tempString);
		if (TXT_LENGTH(tempString) < DEVICE_FILE_NAME_LENGTH)
		{
			_tcsncpy_s(tempString, DEVICE_FILE_NAME, DEVICE_FILE_NAME_LENGTH);
		}
	}

	if (!PicFuncs.ReadDeviceFile(tempString))
	{
		cout << format(DEVICE_FILE_NAME " device file not found.") << endl;
		ReturnCode = DEVICEFILE_ERROR;
		loadDeviceFileFailed = true;
	}
	else
	{
		char compatMinLevel = DevFileCompatLevelMin;
		if (PicFuncs.DevFile.Info.Compatibility < compatMinLevel)
		{
			cout << format(DEVICE_FILE_NAME " device file is too old.") << endl;
			ReturnCode = DEVICEFILE_ERROR;
			loadDeviceFileFailed = true;
		}
		if (PicFuncs.DevFile.Info.Compatibility > DevFileCompatLevel)
		{
			cout << format(DEVICE_FILE_NAME " device file requires an update of pk2cmd.") << endl;
			ReturnCode = DEVICEFILE_ERROR;
			loadDeviceFileFailed = true;
		}
	}

	// Check for help display requests that need the device file.
	if (checkHelp2(args, loadDeviceFileFailed) || loadDeviceFileFailed)
	{
		return;
	}

	// Check for Pk2Operation
	Pk2Operation = Pk2OperationCheck(args);

	// Look for PICkit 2
	if (Pk2Operation)
	{
		if (!selectUnitArg(args))
			return; // just listing units

		if (!findPICkit2(pk2UnitIndex))
		{
			if (ReturnCode == WRONG_OS)
				bootloadArg(args); // see if -d found
			return;
		}
	}

	// execute commands
	processArgs(args);

	if (Pk2Operation)
	{
		int status = PicFuncs.ReadPkStatus();
		if ((STATUS_VDD_ERROR & status) > 0)
		{
			cout << format("VDD Error detected.  Check target for proper connectivity.") << endl;
			ReturnCode = VOLTAGE_ERROR;
		}
		else if ((STATUS_VPP_ERROR & status) > 0)
		{
			cout << format("VPP Error detected.  Check target for proper connectivity.") << endl;
			ReturnCode = VOLTAGE_ERROR;
		}
	}

}

void Ccmd_app::ResetAtExit(void)
{
	if (resetOnExit)
	{
		cout << format("Resetting PICkit 2...") << endl;
		PicFuncs.ResetPICkit2(); // must re-enumerate with new UnitID in serial string
	}
}

bool Ccmd_app::Pk2OperationCheck(TextVec& args)
{
	int i;
	bool ret = false;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];
		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{ // these options require PICkit 2 communications.
				case 'A':
				case 'a':
				case 'C':
				case 'c':
				case 'D':
				case 'd':
				case 'E':
				case 'e':
				case 'G':
				case 'g':
				case 'I':
				case 'i':
				case 'M':
				case 'm':
				case 'N':
				case 'n':
				case 'R':
				case 'r':
				case 'S':
				case 's':
				case 'T':
				case 't':
				case 'U':
				case 'u':
				case 'V':
				case 'v':
				case 'W':
				case 'w':
				case 'X':
				case 'x':
				case 'Y':
				case 'y':
				case 'Z':
				case 'z':
					ret = true;
					break;

				case 'P':
				case 'p':
					//if (((*parg)[2]) == 0)
					//	ret = true; // auto detect
					//if (((((*parg)[2]) == 'F') || (((*parg)[2]) == 'f')) && (((*parg)[3]) != 0))
					//	ret = true; // family detect
					ret = true; // always true so Vdd gets shut off, MCLR is released
					break;

				default:
					break;
			}

			if (ret)
				return true;
		}
	}
	return false;
}

void Ccmd_app::processArgvForSpaces(TextVec& args)
{
	int	i, j;

	// Blank nargv.

	//nargc = 0;

	//for (i=0; i < K_MAX_ARGS; i++)
	//	nargv[i] = NULL;

	nargs.clear();

	for (i=0, j=0; i < NSIZE(args); i++, j++)
	{
		_TCHAR** parg = &args[i];

		nargs.push_back( (char *) malloc(MAX_PATH) );
		strcpy(nargs[j], *parg);

		if (checkSwitch(*parg))
		{
			if ((i < (NSIZE(args) - 1)) && (strlen(*parg) == 2)) // only append next string if first is just option
			{
				if (!checkSwitch(args[i + 1]))
				{
					++i;
					strcat(nargs[j], args[i]);
				}
			}
		}
	}

}

void Ccmd_app::processArgs(TextVec& args)
{
	int i;
	_TCHAR tempString[MAX_PATH] = "";

	// make sure VDD is off
	if (Pk2Operation)
	{
		PicFuncs.VddOff();
	}

	if (bootloadArg(args)) // ignore all other commands if -d found
		return;

	if (unitIDArg(args))	// ignore other commands if -n found
		return;

	_TCHAR** parg = NULL;

	// look for part name first
	for (i = 0; i < NSIZE(args); i++)
	{
		parg = &args[i];
		if (checkSwitch(*parg))
		{
			if (((*parg)[1] == 'P') || ((*parg)[1] == 'p'))
				break;
		}
	}
	if (i == NSIZE(args))
	{ // no part specified
		cout << format("-P is a required option") << endl << endl;
		ReturnCode = INVALID_CMDLINE_ARG;
		return;
	}
	XCOPY28(tempString, XRIGHT(*parg,2));
	*parg = (char *) ""; // blank argument, we've already processed it.
	string2Upper(tempString, MAX_PATH);

	// auto detect?
	if (tempString[0] == 0) 
	{ // no argument, full autodetect
		if (detectAllFamilies(args))
		{ // found a device
			XCOPY28(tempString, PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].PartName);
			cout << format("Auto-Detect: Found part %s.", tempString) << endl << endl;
		}
		else if (ReturnCode == INVALID_CMDLINE_ARG)
		{ // arg error
			return;
		}
		else
		{
			cout << format("Auto-Detect: No known part found.") << endl << endl;
			ReturnCode = AUTODETECT_FAILED;
			return;
		}
	}
	else if (((tempString[0] == 'f') || (tempString[0] == 'F')) && (tempString[1] == 0))
	{ // print family ID numbers
		printFamilies();
		return;
	}
	else  if (((tempString[0] == 'f') || (tempString[0] == 'F')) && (tempString[1] != 0))
	{ // auto detect family
		if (detectSpecificFamily(XRIGHT(tempString,1), args))
		{ // found a device
			XCOPY28(tempString, PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].PartName);
			cout << format("Auto-Detect found part %s.", tempString) << endl << endl;
		}
		else if (ReturnCode == INVALID_CMDLINE_ARG)
		{ // arg error
			return;
		}
		else
		{ // detect failed
			ReturnCode = AUTODETECT_FAILED;
			return;
		}
	}

	// look for the device in the device file - still need to do this on autodetect to properly set up buffers.
	if(!PicFuncs.FindDevice(tempString))
	{
		cout << format("Could not find device %s.", tempString) << endl << endl;
		ReturnCode = INVALID_CMDLINE_ARG;
		return;
	}

	if (!priority1Args(args, false))
		return;
	if (!checkArgsForBlankCheck(args))
	{
		cout << format("-C Blank Check must be done independent of other programming commands.") << endl;
		ReturnCode = INVALID_CMDLINE_ARG;
		return;
	}
	if (!priority2Args(args))
		return;

	if (!priority3Args(args))
	return;

	if (!priority4Args(args))
	return;

	if (!delayArg(args))
	return;

	// unrecognized commands ignored.
}

bool Ccmd_app::detectAllFamilies(TextVec& args)
{
	// on auto detect, must run these args first
	if (!priority1Args(args, true))
		return false;
    for (int searchIndex = 0; searchIndex < PicFuncs.DevFile.Info.NumberFamilies ; searchIndex++)
    {
		for (int searchPriority = 0; searchPriority < PicFuncs.DevFile.Info.NumberFamilies; searchPriority++)
		{
			if ((PicFuncs.DevFile.Families[searchPriority].PartDetect) && (PicFuncs.DevFile.Families[searchPriority].SearchPriority == searchIndex))
			{
				if (PicFuncs.SearchDevice(searchPriority))   
				{
					return true;
				}
			}
		}
    }
    return false; // no supported part found in any family
}

void Ccmd_app::printFamilies(void)
{
	cout << endl << format("Auto-Detectable Part Families:") << endl << endl;
	cout << format("     ID#  Family") << endl;
	int familyID = 0;
    for (int index = 0; index < PicFuncs.DevFile.Info.NumberFamilies ; index++)
    {
		for (int order = 0; order < PicFuncs.DevFile.Info.NumberFamilies; order++)
		{
			if ((PicFuncs.DevFile.Families[order].FamilyType == index) && PicFuncs.DevFile.Families[order].PartDetect)
			{
				cout << format("     %2i   %s", familyID++, PicFuncs.DevFile.Families[order].FamilyName) << endl;
			}
		}
    }
}

bool Ccmd_app::detectSpecificFamily(_TCHAR* idString, TextVec& args)
{
	int familyID = 0;

	if (!getValue((unsigned int*)&familyID, idString))
	{
		cout << format("-PF Illegal family ID value.") << endl;
		ReturnCode = INVALID_CMDLINE_ARG;
		return false;
	}
	// on auto detect, must run these args first
	if (!priority1Args(args, true))
		return false;

	int idNumber = 0;
    for (int index = 0; index < PicFuncs.DevFile.Info.NumberFamilies ; index++)
    {
		for (int order = 0; order < PicFuncs.DevFile.Info.NumberFamilies; order++)
		{
			if ((PicFuncs.DevFile.Families[order].FamilyType == index) && PicFuncs.DevFile.Families[order].PartDetect)
			{
				if (idNumber++ == familyID)
				{
					if (PicFuncs.SearchDevice(order))   
					{
						return true;
					}
					cout << format("Auto-Detect: No known %s part found.", PicFuncs.DevFile.Families[order].FamilyName) << endl << endl;
					return false;
				}
			}
		}
    }
	cout << format("-PF Illegal family ID value.") << endl;
	ReturnCode = INVALID_CMDLINE_ARG;
	return false;
}

bool Ccmd_app::bootloadArg(TextVec& args)
{
	int i, j;
	_TCHAR tempString[MAX_PATH] = "";
	bool ret;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -D download OS
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'D') || ((*parg)[1] == 'd')))
		{
			PicFuncs.ClosePICkit2Device();
			if ((pk2UnitIndex > 0) || (PicFuncs.DetectPICkit2Device(1, false)))
			{
				cout << endl << format("To update the PICkit 2 OS, it must be the only unit connected.") << endl;
				ReturnCode = OPFAILURE;
				return true;
			}

			PicFuncs.ClosePICkit2Device();
			PicFuncs.DetectPICkit2Device(0, true);

			XRIGHTCOPY(tempString,*parg,2);
			*parg = (char *) "";
			j = 1;
			while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
			{ // check for path with space(s) in it
				TXT_PUSH_UNSAFE(tempString, " ");
				TXT_PUSH_UNSAFE(tempString, args[i+j]);
				args[i + j++] = (char *) "";
			}
			ret = Pk2BootLoadFuncs.ReadHexAndDownload(tempString, &PicFuncs, pk2UnitIndex);
			if (!ret)
			{
				cout << format("Error opening hex file.") << endl;
				ReturnCode = OPFAILURE;
				return true; // download command found
			}
			ret = Pk2BootLoadFuncs.ReadHexAndVerify(tempString, &PicFuncs);
			if (!ret)
			{
				cout << format("Error validating OS download.") << endl;
				ReturnCode = OPFAILURE;
				return true; // download command found
			}
			ret = PicFuncs.BL_WriteFWLoadedKey();
			if (!ret)
			{
				cout << format("Error with OS download.") << endl;
				ReturnCode = OPFAILURE;
				return true; // download command found
			}
			cout << format("Resetting PICkit 2...") << endl;
			PicFuncs.BL_Reset();
			Sleep(5000);
			if (!PicFuncs.DetectPICkit2Device(pk2UnitIndex, true))
			{
				cout << format("PICkit 2 failed to reset.") << endl;
				ReturnCode = OPFAILURE;
				return true; // download command found
			}
			cout << format("OS Update Successful.") << endl;
			return true;
		}
	}
	return false; // no bootload command
}

bool Ccmd_app::unitIDArg(TextVec& args)
{
	int i, j;
	_TCHAR writeString[MAX_PATH] = "";
	_TCHAR readString[MAX_PATH] = "";
	bool ret;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -N set Unit ID
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'N') || ((*parg)[1] == 'n')))
		{
			XRIGHTCOPY(writeString,*parg,2);
			*parg = (char *) "";
			j = 1;
			while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
			{ // check for name with space(s) in it
				TXT_PUSH_UNSAFE(writeString, " ");
				TXT_PUSH_UNSAFE(writeString, args[i+j]);
				args[i + j++] = (char *) "";
			}
			ret = PicFuncs.UnitIDWrite(writeString);
			if (!ret)
			{
				cout << format("Error writing Unit ID.") << endl;
				ReturnCode = OPFAILURE;
				return true; // unit id command found
			}
			ret = PicFuncs.UnitIDRead(readString);
			if ((TXT_LENGTH(writeString) == 0) && !ret)
			{
				cout << format("Unit ID successfully cleared...") << endl;
				resetOnExit = true;
				return true;
			}
			for (j = 0; j < 14; j++)
			{
				if ((writeString[j] != readString[j]) || !ret)
				{
					cout << format("Error verifying Unit ID.") << endl;
					ReturnCode = OPFAILURE;
					return true; // unit id command found
				}
				if (writeString[j] == 0)
					break;
			}
			cout << format("Unit ID successfully assigned...") << endl;
			resetOnExit = true;
			return true;
		}
	}
	return false; // no unit id command
}

bool Ccmd_app::selectUnitArg(TextVec& args)
{
	int i, j, k, len;
	bool listFWVer = false;
	_TCHAR unitIDString[MAX_PATH] = "";
	_TCHAR readString[MAX_PATH] = "";
	_TCHAR *pUnitID = 0;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -S use Unit ID
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'S') || ((*parg)[1] == 's')))
		{
			if ((TXT_LENGTH(*parg) == 3) && ((*parg)[2] == '#'))
				listFWVer = true;

			if ((TXT_LENGTH(*parg) > 2) && !listFWVer)
			{ // find specific unit
				XRIGHTCOPY(unitIDString,*parg,2);
				*parg = (char *) "";
				for (j = 0; j < 8; j++)
				{
					if (PicFuncs.DetectPICkit2Device(j, false))
					{
						//ret = PicFuncs.UnitIDRead(readString);

						pUnitID = PicFuncs.GetUnitID();
						XRIGHTCOPY(readString, pUnitID, 0);
						if (TXT_COMPARE(readString, "-", 1) != 0) // UnitID not blank
						{
							k = TXT_COMPARE(unitIDString, readString, TXT_LENGTH(unitIDString));
							if (k == 0)
							{
								pk2UnitIndex = j;
								PicFuncs.ClosePICkit2Device();	// we'll re-open it when we check OS version, etc.
								return true;
							}
						}
						PicFuncs.ClosePICkit2Device();
					}
					else
					{
						if (j == 0)
							cout << endl << format("No PICkit 2 Units Found...") << endl;
						else
							cout << endl << format("PICkit 2 with Unit ID '%s' not found.", unitIDString) << endl;
						break;
					}
				}				
				return false;
			}
			else
			{ // list all units, max 8
				for (j = 0; j < 8; j++)
				{
					if (PicFuncs.DetectPICkit2Device(j, listFWVer))
					{
						if (j == 0)
						{
							if (listFWVer)
								cout << endl << format("Unit #     Unit ID          OS Firmware") << endl;
							else
								cout << endl << format("Unit #     Unit ID") << endl;
						}

						//if ((PicFuncs.FirmwareVersion.major < 2) || (PicFuncs.FirmwareVersion.major == 'v'))
						//	ret = 0;
						//else
						//{
						//	ret = PicFuncs.UnitIDRead(readString);
						//}
							
						pUnitID = PicFuncs.GetUnitID();
						XRIGHTCOPY(readString, pUnitID, 0);

						//if (ret)
						//{
						//	len = cout << format("%d          %s", j, readString);
						//}
						//else
						//{
						//	len = cout << format("%d          -", j);
						//}
						if (TXT_COMPARE(readString, "PIC18F2550", 10) == 0)
						{
							string stmp;
							if (listFWVer)
								stmp = format("%d          -", j);
							else
								stmp = format("%d          <bootloader>", j);

							cout << stmp;
							len = NSIZE(stmp);
						}
						else
						{
							string stmp;
							stmp = format("%d          %s", j, readString);
							cout << stmp;
							len = NSIZE(stmp);
						}

						while (len < 28)
						{
							cout << format(" ");
							len++;
						}
						
						if (listFWVer)
						{
							if (PicFuncs.FirmwareVersion.major == 'v')
								cout << format("<bootloader>");
							else
								cout << format("%d.%02d.%02d",
									PicFuncs.FirmwareVersion.major,
									PicFuncs.FirmwareVersion.minor,
									PicFuncs.FirmwareVersion.dot); 
						}

						cout << endl;
						PicFuncs.ClosePICkit2Device();
					}
					else
					{
						if (j == 0)
							cout << endl << format("No PICkit 2 Units Found...") << endl;
						break;
					}
				}
				return false;
			}
		}
	}
	return true;
}

int Ccmd_app::getPk2UnitIndex(void)
{
	return pk2UnitIndex;
}

void Ccmd_app::string2Upper(_TCHAR* lcstring, int maxLength)
{
	for (int i = 0; i < maxLength; i++)
	{
		if (*(lcstring + i) == 0)
			break;
		else
			*(lcstring + i) = TCH_UPPER(*(lcstring + i));
	}
}

bool Ccmd_app::priority1Args(TextVec& args, bool preserveArgs)
{	// returns false if any command has an error.

	// priority 1 args are -A, -F, -J, -Q, -V, -W, -X, -Z
	// These can be processed before any programming communications
	int i, j;
	unsigned int tempi;
	float tempf;
	bool ret = true;

	_TCHAR tempString[MAX_PATH] = "";

	// Get default Vdd & Vpp
	if (preserveArgs)
	{
		PicFuncs.SetVddSetPoint(3.0); // 3 Volts always used for part detect.
	}
	else
	{
		PicFuncs.GetDefaultVdd();
		PicFuncs.GetDefaultVpp();
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{
				case 'A':
				case 'a':
					// Set VDD voltage
					if (!preserveArgs) // skip during auto-detect
					{
						tempf = (float)TXT_TO_DOUBLE(XRIGHT(*parg,2));
						if (tempf > PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].VddMax)
						{
							cout << format("-A Vdd setpoint exceeds maximum for this device of %.1fV",
									PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].VddMax) << endl;
							ret = false;
							ReturnCode = INVALID_CMDLINE_ARG;
						}
						if (tempf < 2.5)
						{
							cout << format("-A Vdd setpoint below PICkit 2 minimum of 2.5V") << endl;
							ret = false;
							ReturnCode = INVALID_CMDLINE_ARG;
						}
						PicFuncs.SetVddSetPoint(tempf);
						*parg = (char *)"";
					}
					break;

				case 'F':
				case 'f':
					if (!preserveArgs) // skip if still looking for a part
					{
						// Hex File Selection
						XRIGHTCOPY(tempString,*parg,2);
						*parg = (char *) "";
						j = 1;
						while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
						{ // check for path with space(s) in it
							TXT_PUSH_UNSAFE(tempString, " ");
							TXT_PUSH_UNSAFE(tempString, args[i+j]);
							if (!preserveArgs)
								args[i + j] = (char *) "";
							j++;
						}
						// Check for BIN file:
						ret = false; // assume not bin file
						j = TXT_LENGTH(tempString);
						if (j > 3)
						{ // this is kind of brute force, but avoids a lot of string library calls and another tempstring
							if (tempString[j-1] == ' ') // may have space on the end
							{
								tempString[j-1] = 0; // kill the space
								j--;
							}
							if (((tempString[j-3] == 'b') || (tempString[j-3] == 'B'))
								&& ((tempString[j-2] == 'i') || (tempString[j-2] == 'I'))
								&& ((tempString[j-1] == 'n') || (tempString[j-1] == 'N')))
								ret = true; // BIN file
						}
						if (ret && PicFuncs.FamilyIsEEPROM())
						{ // bin file
							cout << format("Importing -f file as .BIN") << endl;
							ret = ImportExportFuncs.ImportBINFile(tempString, &PicFuncs);
						}
						else
						{ // hex file
							ret = ImportExportFuncs.ImportHexFile(tempString, &PicFuncs);
						}
						if (ret)
							hexLoaded = true;
						else
							ReturnCode = INVALID_HEXFILE;
					}
					break;

				case 'J':
				case 'j':
					// Display percentage operation completion
					if (((*parg)[2] == 'n') || ((*parg)[2] == 'N'))
						PicFuncs.SetTimerFunctions(true, true); // use newlines
					else
						PicFuncs.SetTimerFunctions(true, false);
					if (!preserveArgs)
						*parg = (char *) "";
					break;

				case 'L':
				case 'l':
					// Set ICSP speed
					if ((*parg)[2] == 0)
					{ // no specified value - illegal
						cout << format("-L Invalid value.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					else if (getValue(&tempi, XRIGHT(*parg,2)))
					{
						if (tempi > 16)
							tempi = 16;
						PicFuncs.SetProgrammingSpeedDefault((unsigned char)tempi);
					}
					else
					{ // no specified value - illegal
						cout << format("-L Invalid value.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					if (!preserveArgs)
						*parg = (char *) "";
					break;

				case 'Q':
				case 'q':
					// Disable PE
					PicFuncs.DisablePE33();
					if (!preserveArgs)
						*parg = (char *) "";
					break;

				case 'V':
				case 'v':
					// VPP override
					if (preserveArgs)
					{ // cannot be used with part detect
						cout << format("-V Cannot be used with part auto-detect.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					else
					{
						tempf = (float)TXT_TO_DOUBLE(XRIGHT(*parg,2));
						PicFuncs.SetVppSetPoint(tempf);
						*parg = (char *) "";
					}
					break;

				case 'W':
				case 'w':
					// External power target
					float vdd, vpp;
					PicFuncs.SetSelfPowered(true);
					PicFuncs.ReadPICkitVoltages(&vdd, &vpp);
					PicFuncs.SetVddSetPoint(vdd);
					if (!preserveArgs)
						*parg = (char *) "";
					break;

				case 'X':
				case 'x':
					// VPP first
					if (preserveArgs)
					{ // cannot be used with part detect
						cout << format("-X Cannot be used with part auto-detect.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					else
					{
						if (PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].ProgEntryVPPScript == 0)
						{
							cout << format("-X This part does not support VPP first program mode") << endl;
							ret = false;
							ReturnCode = INVALID_CMDLINE_ARG;
						}
						PicFuncs.SetVppFirstEnable(true);	
							*parg = (char *) "";
					}
					break;

				case 'Z':
				case 'z':
					for (j = 1; j < NSIZE(args); j++)
					{
						if ((checkSwitch(args[j])) && ((args[j][1] == 'M') || (args[j][1] == 'm')))
						{
							preserveEEPROM = true;
						}
					}
					if (!preserveEEPROM)
					{
						cout << format("-Z Preserve EEData must be used in conjunction with the -M program command.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					if (!preserveArgs)
						*parg = (char *) "";
					break;

				default:
					break;
			}
			if (!ret)
				break;	// stop on first error.
		}
	}	
	if (PicFuncs.GetSelfPowered() && PicFuncs.GetVppFirstEnable())
	{
		cout << format("-W -X VPP first not supported with external power") << endl;
		ret = false;
		ReturnCode = INVALID_CMDLINE_ARG;
	}

	return ret;
}

bool Ccmd_app::checkArgsForBlankCheck(TextVec& args)
{	// returns false if there is an error.

	// Blank Check (-C) cannot be used with -E, -G, -M, -U, -Y
	bool blankCheck = false;
	bool contradication = false;

	for (int i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{
				case 'C':
				case 'c':
					blankCheck = true;
					break;

				case 'E':
				case 'e':
					contradication = true;
					break;

				case 'G':
				case 'g':
					contradication = true;
					break;

				case 'M':
				case 'm':
					contradication = true;
					break;

				case 'U':
				case 'u':
					contradication = true;
					break;

				case 'Y':
				case 'y':
					contradication = true;
					break;

				default:
					break;
			}
		}
	}	

	return !(blankCheck && contradication);
}

bool Ccmd_app::priority2Args(TextVec& args)
{	// returns false if any command has an error.

	// priority 2 args are -C, -U, -E, -M, -Y, -G
	// In the order they will be processed.
	int i, j;
	bool program, eedata, config, userid;
	bool ret = true;

	_TCHAR tempString[MAX_PATH] = "";

	// get current date & time
	_TCHAR stime[128] = "";
	time_t now;
	struct tm today;
	_tzset();
	time(&now);
    _localtime64_s(&today, &now);
	_tcsftime( stime, 128, "%d-%b-%Y, %H:%M:%S", &today );


	// Prep PICkit 2 (set Vdd, vpp, download scripts)
	PicFuncs.PrepPICkit2();

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -C Blank Check
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'C') || ((*parg)[1] == 'c')) && ret)
		{
			if (PicFuncs.FamilyIsKeeloq())
			{
				cout << format("BlankCheck not supported for KEELOQ devices.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
			else if (PicFuncs.FamilyIsMCP())
			{
				cout << format("BlankCheck not supported for MCP devices.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
			else if (PicFuncs.ReadDevice(BLANK_CHECK, true, true, true, true))
			{
				cout << format("Device is blank") << endl;
			}
			else
			{
				cout << format("%s memory is NOT blank.", PicFuncs.ReadError.memoryType) << endl << endl;
				printMemError();
				ret = false;
			}
		}
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -U Overwrite Cal
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'U') || ((*parg)[1] == 'u')) && ret)
		{
			if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].OSSCALSave)
			{
				for (j = 1; j < NSIZE(args); j++)
				{
					if ((checkSwitch(args[j])) && ((args[j][1] == 'M') || (args[j][1] == 'm')))
					{
						ret = getValue(&PicFuncs.DeviceBuffers->OSCCAL, XRIGHT(*parg,2));
						if (ret)
						{
							PicFuncs.OverwriteOSCCAL = true;
						}
						else
						{
							cout << format("-U Error parsing value.") << endl;
							ReturnCode = INVALID_CMDLINE_ARG;
						}
					}
				}
				if (!PicFuncs.OverwriteOSCCAL)
				{
					cout << format("-U Overwrite OSCCAL must be used in conjunction with the -M program command.") << endl;
					ret = false;
					ReturnCode = INVALID_CMDLINE_ARG;
				}
			}
			else
			{
					cout << format("-U Overwrite OSCCAL cannot be used with this device.") << endl;
					ret = false;
					ReturnCode = INVALID_CMDLINE_ARG;
			}
		}
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -E Erase
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'E') || ((*parg)[1] == 'e')) && ret)
		{
			if (PicFuncs.FamilyIsKeeloq())
			{
				cout << format("Erase not supported for KEELOQ devices.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
			else if (PicFuncs.FamilyIsMCP())
			{
				cout << format("Erase not supported for MCP devices.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
			else if (PicFuncs.FamilyIsEEPROM() 
				&& (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigMasks[PROTOCOL_CFG] != MICROWIRE_BUS)
				&& (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigMasks[PROTOCOL_CFG] != UNIO_BUS))
			{ // Microwire / UNIO have a true "chip erase".  Other devices must write every byte blank.
				cout << format("Erasing Device...") << endl;
				if (!PicFuncs.SerialEEPROMErase())
				{
					ret = false;
					ReturnCode = OPFAILURE;
				}
			}
			else
			{
				cout << format("Erasing Device...") << endl;
				PicFuncs.EraseDevice(true, !preserveEEPROM, &usingLowVoltageErase);
			}
		}
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -M Program 
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'M') || ((*parg)[1] == 'm')) && ret)
		{
			if (hexLoaded)
			{
				bool noProgEntryForVerify = false;
				bool verify = true;
				bool argError = true;
								
				if ((*parg)[2] == 0)
				{ // no specified region - erase then program all
					if (PicFuncs.FamilyIsEEPROM())
					{
						ret = PicFuncs.EepromWrite(WRITE_EE);
						verify = ret;
						argError = ret;
						if (!ret)
						{
							ReturnCode = PGMVFY_ERROR;
						}
						program = true;
						eedata = false;
						userid = false;
						config = false;
					}
					else
					{
						bool rewriteEE = PicFuncs.EraseDevice(true, !preserveEEPROM, &usingLowVoltageErase);
						program = true;
						eedata = (rewriteEE || !preserveEEPROM);
						userid = true;
						config = true;
						if (PicFuncs.FamilyIsPIC32())
						{
							// Program everything.
							noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
						}
						else
						{
							// program all but configs and verify, as configs may contain code protect
							noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, false, usingLowVoltageErase);
						}
						if (!noProgEntryForVerify)
						{ // if it is true, then configs are in program memory
							ret = PicFuncs.ReadDevice(VERIFY_MEM_SHORT, program, eedata, userid, false);
							verify = ret;
							argError = ret;
							if (ret)
							{ // now program configs
								program = false;
								eedata = false;
								userid = false;
								noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
							}
							else
							{
								ReturnCode = PGMVFY_ERROR;
							}
						}
					}
				}
				else
				{
					program = false;
					eedata = false;
					userid = false;
					config = false;
					for (j = 2; j < TXT_LENGTH(*parg); j++)
					{
						switch ((*parg)[j])
						{
							case 'p':
							case 'P':
								program = true;
								if (PicFuncs.FamilyIsEEPROM())
								{
									ret = PicFuncs.EepromWrite(WRITE_EE);
									argError = ret;
									verify = ret;
								}
								else
								{
									noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
								}
								break;

							case 'e':
							case 'E':
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].EEMem > 0)
								{
									if (preserveEEPROM)
									{
										cout << format("Cannot both program and preserve EEData memory.") << endl;
										ReturnCode = INVALID_CMDLINE_ARG;
										ret = false;
									}
									else
									{
										eedata = true;
										noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
									}
								}
								else
									ret = false;
								break;

							case 'i':
							case 'I':
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].UserIDWords > 0)
								{
									userid = true;
									noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
								}
								else
									ret = false;
								break;

							case 'c':
							case 'C':
								// check for devices with config in program memory - can't program seperately.
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords > 0)
								{
									    int configLocation = (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigAddr /
																PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].ProgMemHexBytes;
										int configWords = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords;
										if ((configLocation < (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ProgramMem) && (configWords > 0))
										{
											cout << format("This device has configuration words in Program Memory.") << endl << format("They cannot be programmed separately.") << endl;
											ReturnCode = INVALID_CMDLINE_ARG;
											ret = false;
										}
										else
										{
											config = true;
											noProgEntryForVerify = PicFuncs.WriteDevice(program, eedata, userid, config, usingLowVoltageErase);
										}
								}
								else
									ret = false;
								break;

							case 'v':
							case 'V':
								{
								XRIGHTCOPY(tempString,*parg,3);
								*parg = (char *) "";
								int k = 1;
								if (((i+k) < NSIZE(args)) && (!checkSwitch(args[i+k])))
								{ // check for space after v
									TXT_PUSH_UNSAFE(tempString, args[i+k]);
									args[i + k++] = (char *) "";
								}
								int vtop = 0;
								int vbot = 0;
								ret = getRange(&vtop, &vbot, tempString);
								if (ret)
								{
									PicFuncs.WriteVector(vtop, vbot);
								}
								}
								break;

							default:
								ret = false;
						}
					}
				}
				if (ret || !argError)
				{
					if (verify)
					{
						if (noProgEntryForVerify)
							ret = PicFuncs.ReadDevice(VERIFY_NOPRG_ENTRY, program, eedata, userid, config);
						else
							ret = PicFuncs.ReadDevice(VERIFY_MEM_SHORT, program, eedata, userid, config);
					}
					cout << format("PICkit 2 Program Report") << endl;
					cout << format("%s", stime) << endl;
					cout << format("Device Type: %s", PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].PartName) << endl << endl;
					if (ret)
					{
						cout << format("Program Succeeded.") << endl;
					}
					else
					{
						cout << format("%s Memory Errors", PicFuncs.ReadError.memoryType) << endl << endl;
						printMemError();
						ReturnCode = PGMVFY_ERROR;
					}
				}
				else
				{
					cout << format("Invalid Memory region entered for program") << endl;
					ReturnCode = INVALID_CMDLINE_ARG;
				}
			}
			else
			{
				cout << format("No Image loaded.") << endl << format("Please load a hex file before programming or verifying.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
		}
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -Y Verify
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'Y') || ((*parg)[1] == 'y')) && ret)
		{
			if (hexLoaded)
			{

				if (PicFuncs.FamilyIsKeeloq())
				{
					cout << format("Verify not supported for KEELOQ devices.") << endl;
					ret = false;
				}
				else if ((*parg)[2] == 0)
				{ // no specified region - verify all
					program = true;
					eedata = true;
					userid = true;
					config = true;
				}
				else
				{
					program = false;
					eedata = false;
					userid = false;
					config = false;
					for (j = 2; j < TXT_LENGTH(*parg); j++)
					{
						switch ((*parg)[j])
						{
							case 'p':
							case 'P':
								program = true;
								break;

							case 'e':
							case 'E':
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].EEMem > 0)
									eedata = true;
								else
									ret = false;
								break;

							case 'i':
							case 'I':
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].UserIDWords > 0)
									userid = true;
								else
									ret = false;
								break;

							case 'c':
							case 'C':
								if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords > 0)
								{
									int configLocation = (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigAddr /
															PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].ProgMemHexBytes;
									int configWords = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords;
									if ((configLocation < (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ProgramMem) && (configWords > 0))
									{
										cout << format("This device has configuration words in Program Memory.") << endl;
										ReturnCode = INVALID_CMDLINE_ARG;
										ret = false;
									}
									else
										config = true;
								}	
								else
									ret = false;
								break;

							default:
								ret = false;
						}
					}
				}
				if (ret)
				{
					ret = PicFuncs.ReadDevice(VERIFY_MEM, program, eedata, userid, config);
					cout << format("PICkit 2 Verify Report") << endl;
					cout << format("%s", stime) << endl;
					cout << format("Device Type: %s", PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].PartName) << endl << endl;
					if (ret)
					{
						cout << format("Verify Succeeded.") << endl;
					}
					else
					{
						cout << format("%s Memory Errors", PicFuncs.ReadError.memoryType) << endl << endl;
						printMemError();
						ReturnCode = PGMVFY_ERROR;
					}
				}
				else
				{
					cout << format("Invalid Memory region entered for verify") << endl;
					ReturnCode = INVALID_CMDLINE_ARG;
				}
			}
			else
			{
				cout << format("No Image loaded.") << endl << format("Please load a hex file before programming or verifying.") << endl;
				ReturnCode = INVALID_CMDLINE_ARG;
				ret = false;
			}
		}
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		// -G Read 
		if ((checkSwitch(*parg)) && (((*parg)[1] == 'G') || ((*parg)[1] == 'g')) && ret)
		{
			int startAddr = 0;
			int stopAddr = 0;

			if ((*parg)[2] == 0)
			{ // no specified type - illegal
				ret = false;
			}
			else if (PicFuncs.FamilyIsKeeloq())
			{
				cout << format("Read not supported for KEELOQ devices.") << endl;
				ret = false;
			}
			else
			{
				switch ((*parg)[2])
				{
					case 'f':
					case 'F':
							if (PicFuncs.ReadDevice(READ_MEM, true, true, true, true))
							{
								XRIGHTCOPY(tempString,*parg,3);
								*parg = (char *) "";
								j = 1;
								while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
								{ // check for path with space(s) in it
									if (TXT_LENGTH(tempString) != 0) // don't add space if it's between "F" and start of filename
										TXT_PUSH_UNSAFE(tempString, " ");
									TXT_PUSH_UNSAFE(tempString, args[i+j]);
									args[i + j++] = (char *) "";
								}
								// Check for BIN file:
								ret = false; // assume not bin file
								j = TXT_LENGTH(tempString);
								if (j > 3)
								{ // this is kind of brute force, but avoids a lot of string library calls and another tempstring
									if (tempString[j-1] == ' ') // may have space on the end
									{
										tempString[j-1] = 0; // kill the space
										j--;
									}
									if (((tempString[j-3] == 'b') || (tempString[j-3] == 'B'))
										&& ((tempString[j-2] == 'i') || (tempString[j-2] == 'I'))
										&& ((tempString[j-1] == 'n') || (tempString[j-1] == 'N')))
										ret = true; // BIN file
								}
								if (ret && PicFuncs.FamilyIsEEPROM())
								{ // BIN file
									cout << format("Exporting -gf file as .BIN") << endl;
									ret = ImportExportFuncs.ExportBINFile(tempString, &PicFuncs);
								}
								else
								{ // hex file
									ret = ImportExportFuncs.ExportHexFile(tempString, &PicFuncs);
								}
								if (ret)
								{
									cout << format("Read successfully.") << endl;
									hexLoaded = true;
								}
								else
									ReturnCode = FILE_OPEN_ERROR;
							}
							else
							{
								cout << endl << format("Read Error") << endl;
								ReturnCode = OPFAILURE;
							}
						break;

					case 'p':
					case 'P':
						// Read Program mem range to screen
						XRIGHTCOPY(tempString,*parg,3);
						*parg = (char *) "";
						j = 1;
						if (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
						{ // check for space after p
							TXT_PUSH_UNSAFE(tempString, args[i+j]);
							args[i + j++] = (char *) "";
						}
						ret = getRange(&startAddr, &stopAddr, tempString);
						if (ret)
						{
							if (PicFuncs.ReadDevice(READ_MEM, true, false, false, false))
							{
								cout << format("Read successfully.") << endl;
								printProgramRange(startAddr, stopAddr);
							}
							else
							{
								cout << endl << format("Read Error") << endl;
								ReturnCode = OPFAILURE;
							}
						}
						break;

					case 'e':
					case 'E':
						if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].EEMem > 0)
						{
							// Read EE mem range to screen
							XRIGHTCOPY(tempString,*parg,3);
							*parg = (char *) "";
							j = 1;
							if (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
							{ // check for space after p
								TXT_PUSH_UNSAFE(tempString, args[i+j]);
								args[i + j++] = (char *) "";
							}
							ret = getRange(&startAddr, &stopAddr, tempString);
							if (ret)
							{
								if (PicFuncs.ReadDevice(READ_MEM, false, true, false, false))
								{
									cout << format("Read successfully.") << endl;
									printEEDataRange(startAddr, stopAddr);
								}
								else
								{
									cout << endl << format("Read Error") << endl;
									ReturnCode = OPFAILURE;
								}
							}
						}
						else
							ret = false;
						break;

					case 'i':
					case 'I':
						if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].UserIDWords > 0)
						{
							// Read User IDs to screen
							if (PicFuncs.ReadDevice(READ_MEM, false, false, true, false))
								{
									cout << format("Read successfully.") << endl;
									printUserIDs();
								}
								else
								{
									cout << endl << format("Read Error") << endl;
									ReturnCode = OPFAILURE;
								}
						}
						else
							ret = false;
						break;

					case 'c':
					case 'C':
						if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords > 0)
						{

						    int configLocation = (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigAddr /
													PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].ProgMemHexBytes;
							int configWords = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords;
							if ((configLocation < (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ProgramMem) && (configWords > 0))
							{
								cout << format("This device has configuration words in Program Memory.") << endl;
								ReturnCode = INVALID_CMDLINE_ARG;
								ret = false;
							}
							else
							{
								// Read Configuration to screen
								if (PicFuncs.ReadDevice(READ_MEM, false, false, false, true))
									{
										cout << format("Read successfully.") << endl;
										printConfiguration();
									}
									else
									{
										cout << endl << format("Read Error") << endl;
										ReturnCode = OPFAILURE;
									}
							}
						}
						else
							ret = false;
						break;

					case 'v':
					case 'V':
						{
						unsigned int vector = PicFuncs.ReadVector();
						cout << format("%8X", vector) << endl;
					    break;
						}

					default:
						ret = false;
				}
			}
			if (!ret)
			{
				if (ReturnCode != FILE_OPEN_ERROR)
				{
					cout << format("Illegal read parameter entered.") << endl;
					ReturnCode = INVALID_CMDLINE_ARG;
				}
			}
		}
	}	

	return ret;
}

bool Ccmd_app::priority3Args(TextVec& args)
{	// returns false if any command has an error.

	// priority 3 args are -I, -K
	// These can be processed after any programming communications
	int i, j;
	bool ret = true;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{
				case 'I':
				case 'i':
					// Display Device ID
					if (PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].PartDetect)
					{
						int deviceID = PicFuncs.ReadDeviceID();
						cout << format("Device ID = %04X", deviceID) << endl;
						cout << format("Revision  = %04X", PicFuncs.GetDeviceRevision()) << endl;
						// Display the device name matching the ID, not necessary the active device.
						if (deviceID == 0)
						{
							cout << format("Device Name = <no device>") << endl;
						}
						else
						{
							for (j = 1; j < PicFuncs.DevFile.Info.NumberParts; j++)
							{
								if ((PicFuncs.DevFile.PartsList[j].Family == PicFuncs.ActiveFamily) && (deviceID == (int)PicFuncs.DevFile.PartsList[j].DeviceID) && PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].PartDetect)
								{
									if (j == PicFuncs.ActivePart)
									{
										cout << format("Device Name = %s", PicFuncs.DevFile.PartsList[j].PartName) << endl;
									}
									else
									{
										cout << format("Device Name = %s   !WARNING! -P device mismatch", PicFuncs.DevFile.PartsList[j].PartName) << endl;
									}
									break;
								}
							}
							if (j == PicFuncs.DevFile.Info.NumberParts)
							{ // no matching device found.
								cout << format("Device Name = <unknown device>") << endl;
							}
						}
					}
					else
					{
						cout << format("This device does not have a Device ID.") << endl;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					break;

				case 'K':
				case 'k':
					// Calculate Checksum
					if (hexLoaded)
					{
						cout << format("Checksum = %04X", PicFuncs.ComputeChecksum()) << endl;
					}
					else
					{
						cout << format("The checksum can only be calculated when a hex file is loaded or written.") << endl;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					break;

				default:
					break;
			}
			if (!ret)
				break;	// stop on first error.
		}
	}	
	return ret;
}

bool Ccmd_app::priority4Args(TextVec& args)
{	// returns false if any command has an error.

	// priority 4 args are -R, -T
	// These can be processed after any programming communications
	int i;
	bool ret = true;

	if (Pk2Operation)
	{
		PicFuncs.SetMCLR(true); // ensure /MCLR asserted.
		if (!PicFuncs.GetSelfPowered())
			PicFuncs.VddOff();      // ensure VDD off if no -T
	}

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{
				case 'R':
				case 'r':
					// Release /MCLR
					PicFuncs.SetMCLR(false);
					break;

				case 'T':
				case 't':
					// Power Target
					if (PicFuncs.GetSelfPowered())
					{
						cout << format("-W -T Cannot power an externally powered target.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					else
						PicFuncs.VddOn();
					break;

				default:
					break;
			}
			if (!ret)
				break;	// stop on first error.
		}
	}	
	return ret;
}

bool Ccmd_app::delayArg(TextVec& args)
{	// returns false if command has an error.

	// delay arg is -H
	// This must be processed last
	int i;
	unsigned int seconds;
	bool ret = true;
	struct termios	tios;

	for (i = 1; i < NSIZE(args); i++)
	{
		_TCHAR** parg = &args[i];

		if (checkSwitch(*parg))
		{
			switch((*parg)[1])
			{
				case 'H':
				case 'h':
					// Delay before exit
					if ((*parg)[2] == 0)
					{ // no specified value - illegal
						cout << format("-H Invalid value.") << endl;
						ret = false;
						ReturnCode = INVALID_CMDLINE_ARG;
					}
					else
					{
						if (((*parg)[2] == 'K') || ((*parg)[2] == 'k'))
						{
							cout << endl << format("Press any key to exit.") << endl;
							tcgetattr(0, &tios);
							tios.c_lflag &= (~(ICANON | ECHO));
							tcsetattr(0, TCSANOW, &tios);
							getc(stdin);
							tios.c_lflag |= (ICANON | ECHO);
							tcsetattr(0, TCSANOW, &tios);
						}
						else if (getValue(&seconds, XRIGHT(*parg,2)))
						{
							if (seconds == 0)
							{ // bad value
								cout << format("-H Invalid value.") << endl;
								ret = false;
								ReturnCode = INVALID_CMDLINE_ARG;
							}
							else
							{
								cout << endl << format("Delaying %d seconds before exit.", seconds) << endl;
								PicFuncs.DelaySeconds(seconds);
							}
						}
						else 
						{ // bad value
							cout << format("-H Invalid value.") << endl;
							ret = false;
							ReturnCode = INVALID_CMDLINE_ARG;
						}
					}
					break;

				default:
					break;
			}
			if (!ret)
				break;	// stop on first error.
		}
	}	
	return ret;
}

void Ccmd_app::printProgramRange(int startAddr, int stopAddr)
{
	int addrInc = PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].AddressIncrement;
	int startWord = startAddr / addrInc;
	int stopWord = stopAddr / addrInc;
	int col;

	if (stopWord < startWord)
		stopWord = startWord;

	if (stopWord >= (int)PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ProgramMem)
		stopWord = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ProgramMem - 1;

	cout << endl << format("Program Memory");

	do
	{ // address loop
		cout << endl << format("%08X ", startWord * addrInc);

		col = 0;
		if (PicFuncs.FamilyIsEEPROM())
		{
			if (PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].BlankValue > 0xFF)
			{
				do
				{ // data columns loop
					cout << format("%04X  ", PicFuncs.DeviceBuffers->ProgramMemory[startWord++]);
					col++;
				} while ((startWord <= stopWord) && (col < 8));
			}
			else
			{
				do
				{ // data columns loop
					cout << format("%02X  ", PicFuncs.DeviceBuffers->ProgramMemory[startWord++]);
					col++;
				} while ((startWord <= stopWord) && (col < 16));
			}
		}
		else
		{
			do
			{ // data columns loop
				cout << format("%06X  ", PicFuncs.DeviceBuffers->ProgramMemory[startWord++]);
				col++;
			} while ((startWord <= stopWord) && (col < 8));
		}

	} while (startWord <= stopWord);
	cout << endl;
}

void Ccmd_app::printEEDataRange(int startAddr, int stopAddr)
{
	int addrInc = PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].EEMemAddressIncrement;
	int startWord = startAddr / addrInc;
	int stopWord = stopAddr / addrInc;
	int col;

	if (stopWord < startWord)
		stopWord = startWord;

	if (stopWord >= PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].EEMem)
		stopWord = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].EEMem -1;

	cout << endl << format("EEData Memory");

	do
	{ // address loop
		cout << endl << format("%04X ", startWord * addrInc);

		col = 0;
		do
		{ // data columns loop
			if (addrInc > 1)
				cout << format("%04X  ", PicFuncs.DeviceBuffers->EEPromMemory[startWord++]);
			else
				cout << format("%02X  ", PicFuncs.DeviceBuffers->EEPromMemory[startWord++]);
			col++;
		} while ((startWord <= stopWord) && (col < 8));

	} while (startWord <= stopWord);
	cout << endl;
}

void Ccmd_app::printUserIDs(void)
{
	int startWord = 0;
	int stopWord = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].UserIDWords -1;
	int col;

	cout << endl << format("ID Memory") << endl;

	do
	{ // address loop
		cout << endl;
		col = 0;
		do
		{ // data columns loop
			if (PicFuncs.DevFile.Families[PicFuncs.ActiveFamily].BlankValue > 0xFFFF)
				cout << format("%06X  ", PicFuncs.DeviceBuffers->UserIDs[startWord++]);
			else
				cout << format("%04X  ", PicFuncs.DeviceBuffers->UserIDs[startWord++]);
			col++;
		} while ((startWord <= stopWord) && (col < 8));

	} while (startWord <= stopWord);
	cout << endl;
}

void Ccmd_app::printConfiguration(void)
{
	int startWord = 0;
	int stopWord = PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigWords -1;
	int col;

	cout << endl << format("Configuration Memory") << endl;

	do
	{ // address loop
		cout << endl;
		col = 0;
		do
		{ // data columns loop
			if (PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].BandGapMask == 0)
				cout << format("%04X  ", (PicFuncs.DeviceBuffers->ConfigWords[startWord]
								& PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigMasks[startWord]));
			else
				cout << format("%04X  ", (PicFuncs.DeviceBuffers->ConfigWords[startWord]
								& PicFuncs.DevFile.PartsList[PicFuncs.ActivePart].ConfigMasks[startWord])
								| PicFuncs.DeviceBuffers->BandGap);
			startWord++;
			col++;
		} while ((startWord <= stopWord) && (col < 8));

	} while (startWord <= stopWord);
	cout << endl;
}

bool Ccmd_app::getRange(int* start, int* stop, _TCHAR* str_range)
{
	int i, j;
	_TCHAR temps[8] = "";

	if (*str_range == 0)
	{ // no range, return (use defaults)
		return true;
	}

	// get start address
	for (i = 0; i < 9; i++)
	{
		if (*(str_range + i) == '-')
		{
			temps[i] = 0;
			break;
		}
		else if (*(str_range + i) == 0)
		 // unexpected end of string
			return false;
		else
			temps[i] = *(str_range + i);			
	}

	if ((i >= 9) || ( i == 0))
	// more than 8 character address or no address
		return false;

	*start = ImportExportFuncs.ParseHex(temps, i++);

	// get stop address
	for (j = 0; j < 9; j++)
	{
		if (*(str_range + i + j) == 0)
		 // end of string
		{
			temps[j] = 0;
			break;
		}
		else
			temps[j] = *(str_range + i + j);			
	}

	if ((j >= 9) || (j ==0))
	// more than 8 character address or no address
		return false;	

	*stop = ImportExportFuncs.ParseHex(temps, j++);
	if (*start <= *stop)
		return true;
	else
		return false;
}

bool Ccmd_app::getValue(unsigned int* value, _TCHAR* str_value)
{
	int i;
	_TCHAR temps[8] = "";

	if (*str_value == 0)
	{ // no value, return error
		return false;
	}

	// get value
	for (i = 0; i < 9; i++)
	{
		if (*(str_value + i) == 0)
		{
			temps[i] = 0;
			break;
		}
		else
			temps[i] = *(str_value + i);			
	}

	if ((i >= 9) || ( i == 0))
	// more than 8 character value or no value
		return false;

	*value = ImportExportFuncs.ParseHex(temps, i++);

	return true;
}

bool Ccmd_app::checkSwitch(_TCHAR * arg)
{
	return ((arg[0] == '-') || (arg[0] == '/'));
}

bool Ccmd_app::findPICkit2(int unitIndex)
{
	unsigned char dot_min = PicFuncs.FW_DOT_MIN;

	if (PicFuncs.DetectPICkit2Device(unitIndex, true))
	{
		if ((PicFuncs.FirmwareVersion.major >= PicFuncs.FW_MAJ_MIN)
			&& (PicFuncs.FirmwareVersion.minor >= PicFuncs.FW_MNR_MIN)
			&& (PicFuncs.FirmwareVersion.dot >= dot_min))
		{
			return true;
		}
		cout << format("PICkit 2 found with Operating System v%d.%02d.%02d", PicFuncs.FirmwareVersion.major,
									PicFuncs.FirmwareVersion.minor, PicFuncs.FirmwareVersion.dot) << endl;
		cout << format("Use -D to download minimum required OS v%d.%02d.%02d or later", PicFuncs.FW_MAJ_MIN,
									PicFuncs.FW_MNR_MIN, PicFuncs.FW_DOT_MIN) << endl;
		ReturnCode = WRONG_OS;
	}
	else
	{
		cout << format("No PICkit 2 found.") << endl;
		ReturnCode = NO_PROGRAMMER;
	}
	return false;
}

void Ccmd_app::printMemError(void)
{
	if (!PicFuncs.FamilyIsPIC32() && !PicFuncs.useProgExec33())
	{
		cout << format("Address   Good     Bad") << endl;
		cout << format("%06X    %06X   %06X", PicFuncs.ReadError.address, PicFuncs.ReadError.expected, PicFuncs.ReadError.read) << endl;
	}
}

bool Ccmd_app::checkDevFilePathOptionB(TextVec& args, _TCHAR* path_string)
{
	_TCHAR path_temp[MAX_PATH];

	int i;

	_TCHAR** parg = NULL;

	// look for 'B' option. 
	for (i = 0; i < NSIZE(args); i++)
	{
		parg = &args[i];
		if (checkSwitch(*parg))
		{
			if (((*parg)[1] == 'b') || ((*parg)[1] == 'B'))
				break;
		}
	}
	if (i == NSIZE(args))
		return false; // -b not found
	if ((*parg)[2] == 0)
	{
		cout << format("-B No path given") << endl;
		ReturnCode = INVALID_CMDLINE_ARG;
		return false;
	}

	// Get path to device file:
	XRIGHTCOPY(path_temp,*parg,2);
	*parg = (char *) "";
	int j = 1;
	while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
	{ // check for path with space(s) in it
		TXT_PUSH_UNSAFE(path_temp, " ");
		TXT_PUSH_UNSAFE(path_temp, args[i+j]);
		args[i + j++] = (char *) "";
	}
	i = 0;
	do
	{
		path_string[i] = path_temp[i];
	}while (path_string[i++] != 0);
	return true;
}

bool Ccmd_app::checkHelp1(TextVec& args)
{ // Helps that don't need the device file.
	int i;

	// if no arguments, display main help screen
	if (NSIZE(args) == 1)
	{
		displayHelp();
		return true;
	}

	_TCHAR** parg = NULL;

	// look for '?' in all arguments.  Display help for first found
	for (i = 0; i < NSIZE(args); i++)
	{
		parg = &args[i];

		if (TXT_SEEK_TCHAR(*parg, '?'))
			break;
	}
	
	if (i == NSIZE(args)) // none found
		return false;

	if (checkSwitch(*parg))
	{
		switch ((*parg)[1])
		{
			case '?':
				if (TXT_LENGTH(*parg) > 2)
				{
					if (((*parg)[2] == 'e') || ((*parg)[2] == 'E'))
					{
						cout
						<< "PK2CMD return codes:" << endl
						<< "Value   Code                    Notes" << endl
						<< "-----   ----                    -----" << endl
						<< "0       OPSUCCESS              -Returned if all selected operations complete" << endl
						<< "                                successfully." << endl
						<< "5       VOLTAGE_ERROR          -A Vdd and/or Vpp voltage error was detected." << endl
						<< "                                This could be due to PICkit 2 being " << endl
						<< "                                improperly connected to a part, incorrect" << endl
						<< "                                part selection, or interference from other" << endl
						<< "                                circuitry on the target board." << endl
						<< "7       OPFAILURE              -Returned if an operation fails and a more" << endl
						<< "                                specific error does not exist." << endl
						<< "10      NO_PROGRAMMER          -The PK2CMD executable is unable to find a" << endl
						<< "                                connected PICkit 2 programmer." << endl
						<< "11      WRONG_OS                -Returned if the OS firmware must be updated" << endl
						<< "                                before being used with this version of" << endl
						<< "                                PK2CMD." << endl
						<< "15      FILE_OPEN_ERROR        -Returned if a file specified for reading to" << endl
						<< "                                (-gf...) cannot be opened for writing." << endl
						<< "24      DEVICEFILE_ERROR       -The PK2CMD executable cannot find the device" << endl
						<< "                                file " DEVICE_FILE_NAME " or the device file" << endl
						<< "                                may be corrupted." << endl
						<< "28      UPDGRADE_ERROR         -Returned when an OS firmware upgade (-d...)" << endl
						<< "                                fails." << endl
						<< "34      PGMVFY_ERROR           -Returned if a program or verify operation" << endl
						<< "                                fails." << endl
						<< "36      INVALID_CMDLINE_ARG    -A syntax error in a command line argument" << endl
						<< "                                was detected, an invalid combination of " << endl
						<< "                                operations was entered, an invalid value was" << endl
						<< "                                entered, or a memory region was selected" << endl
						<< "                                that is not supported by the current device." << endl
						<< "37      INVALID_HEXFILE        -Error opening or loading a specified hex" << endl
						<< "                                file (-f...)." << endl
						<< "39      AUTODETECT_FAILED       A part autodetect operation failed to find" << endl
						<< "                                a known part." << endl << endl;
					}
					else if (((*parg)[2] == 'l') || ((*parg)[2] == 'L'))
					{
						displayLicense();
					}
					else
					{
						//displayHelp();
						return false; // check later for devfile dependent helps
					}
				}
				else
				{
					displayHelp();
				}
				break;

			case 'a':
			case 'A':
				cout << format("Specifies the VDD voltage that the device is programmed at.  The value") << endl;
				cout << format("entered must be less than the allowed maximum of the device and 5.0 Volts") << endl;
				cout << format("(whichever is less), and greater than the allowed minimum of the device and") << endl;
				cout << format("2.5 Volts (whichever is greater).  A default voltage for the device will be") << endl;
				cout << format("used if this command is not specified.") << endl << endl;
				cout << format("The parameter for this command is the floating point value of the desired") << endl;
				cout << format("VDD voltage.") << endl << endl;
				cout << format("Syntax Example -a4.5") << endl;
				break;

			case 'b':
			case 'B':
				cout << format("Specifies the path to the device file " DEVICE_FILE_NAME ".  By default, the") << endl;
				cout << format("directory from which the executable is searched first, then the PATH") << endl;
				cout << format("environment variable.  This option can be used to explicity specify the") << endl;
				cout << format("path to the device file.") << endl << endl;
				cout << format("The parameter for this command is the complete file path to") << endl;
				cout << format(DEVICE_FILE_NAME ", not including the filename.") << endl << endl;
				cout << format("Syntax Example -fc:\\pickit_2\\pk2cmd_dir") << endl;
				break;

			case 'c':
			case 'C':
				cout << format("Checks to see if the device is blank or not. Each memory region (Program,") << endl;
				cout << format("EEPROM, Configuration, and User ID memory) will be checked, and a message") << endl;
				cout << format("indicating whether or not the device is blank, will be displayed. If the") << endl;
				cout << format("device is not blank, the memory region and location of the first error") << endl;
				cout << format("will be displayed.") << endl << endl;
				cout << format("This command takes no parameters.") << endl << endl;
				cout << format("Syntax Example -c") << endl;
				break;

			case 'd':
			case 'D':
				cout << format("Upgrades the firmware on the programmer. This command must be done") << endl;
				cout << format("independently of any other commands.") << endl << endl;
				cout << format("The parameter for this command is the complete file path to the .hex") << endl;
				cout << format("file to be downloaded.") << endl << endl;
				cout << format("Syntax Example -dc:\\filepath\\PK2V021000.hex") << endl;
				break;

			case 'e':
			case 'E':
				cout << format("Erases the device.  A warning will be issued if the device can") << endl;
				cout << format("only be bulk erased and VDD is below the bulk erase voltage.") << endl << endl;
				cout << format("This command takes no parameters.") << endl << endl;
				cout << format("Syntax Example -e") << endl;
				break;

			case 'f':
			case 'F':
				cout << format("Loads a hex file to the programmer. The device will not actually be") << endl;
				cout << format("programmed with the contents of the transferred hex file unless the") << endl;
				cout << format("program command (-m) is also issued.") << endl << endl;
				cout << format("Binary format files are also supported for serial EEPROM devices only.") << endl;
				cout << format("To load a binary file, the filename must end in BIN, ex: myfile.bin") << endl << endl;
				cout << format("The parameter for this command is the complete file path to the hex") << endl;
				cout << format("file to be loaded") << endl << endl;
				cout << format("Syntax Example -fc:\\filepath\\myfile.hex") << endl;
				break;

			case 'g':
			case 'G':
				cout << format("Reads the device and outputs it to either the screen or a hexfile") << endl;
				cout << format("based on the type of read performed. The command must be immediately") << endl;
				cout << format("followed by the type of read, which can be one of the following:") << endl;
				cout << format("     f = Read into hex file. This command must be immediately followed") << endl;
				cout << format("         by the complete file path and name of the file to be created.") << endl;
				cout << format("         Serial EEPROMs only may read into a binary file.  A binary file") << endl;
				cout << format("         will be created if the filename ends in BIN, ex: myfile.bin") << endl;
				cout << format("     p = Read program memory and output the result to the screen. This") << endl;
				cout << format("         command must be immediately followed by the hex address range") << endl;
				cout << format("         to be read, which must be in the form of x-y, where x = start") << endl;
				cout << format("         address and y = end address.") << endl;
				cout << format("     e = Read EEData memory and output the result to the screen. This") << endl;
				cout << format("         command must be immediately followed by the hex address range") << endl;
				cout << format("         to be read, which must be in the form of x-y, where x = start") << endl;
				cout << format("         address and y = end address.") << endl;
				cout << format("     i = Read User ID memory and output the result to the screen. No") << endl;
				cout << format("         further parameters are required for this command.") << endl;
				cout << format("     c = Read Configuration memory and output the result to the screen.") << endl;
				cout << format("         No further parameters are required for this command.") << endl;
				cout << format("Multiple types of read commands can be included in the same command line.") << endl;
				cout << format("NOTE: For HCS and serial EEPROM devices, memory is considered region 'P'") << endl;
				cout << endl;
				cout << format("Syntax Examples -gfc:\\filepath\\myfile") << endl;
				cout << format("                -gp100-200") << endl;
				cout << format("                -gi -ge0-40 -gc") << endl;
				break;

			case 'h':
			case 'H':
				cout << format("If this switch is included, PK2CMD will delay before exiting.  If the value ") << endl;
				cout << format("is set to 'K', then PK2CMD will wait for a keypress before exiting. If the ") << endl;
				cout << format("value is set to a number from 1 to 9, then it will delay the given number") << endl;
				cout << format("of seconds before exiting.") << endl;
				cout << endl;
				cout << format("The parameter for this command is the number of seconds (max = 9) to delay") << endl;
				cout << format("before exiting.  Parameter K will cause it to wait for a keypress.") << endl;
				cout << endl;
				cout << format("Syntax Examples -h3") << endl;
				cout << format("                -hk") << endl;
				break;

			case 'i':
			case 'I':
				cout << format("Reads and displays the value in the Device ID location of the device,") << endl;
				cout << format("as well as the silicon revision code.") << endl;
				cout << endl;
				cout << format("This will also display the device name that matches the returned Device ID,") << endl;
				cout << format("and warn if the Device ID does not match the device specified using the -p") << endl;
				cout << format("command.") << endl;
				cout << endl;
				cout << format("This command takes no parameters.") << endl;
				cout << endl;
				cout << format("Syntax Example -i") << endl;
				break;

			case 'j':
			case 'J':
				cout << format("This switch will display a percentage completion for programming operations") << endl;
				cout << format("instead of the rotating slash.  If the switch is followed by the optional") << endl;
				cout << format("parameter 'n', then each percent update is printed on a newline.  This option") << endl;
				cout << format("is intended for GUI interfaces needing a newline to update the display.") << endl;
				cout << endl;
				cout << format("The optional parameter for this command, N, will print percentage updates") << endl;
				cout << format("on a new line.") << endl;
				cout << endl;
				cout << format("Syntax Examples -j") << endl;
				cout << format("                -jn") << endl;
				break;

			case 'l':
			case 'L':
				cout << format("Sets the ICSP clock (PGC) period to the given value, which must be a value") << endl;
				cout << format("between 1 and 16.  The value specifies the clock period in microseconds.") << endl;
				cout << format("The default value is 1, which gives a period of 1us and an ICSP clock rate") << endl;
				cout << format("of 1 MHz.  A value of 2 gives a period of 2us and a clock rate of 500 kHz.") << endl;
				cout << format("Slowing down the programming clock can help resolve programming issues with") << endl;
				cout << format("heavily loaded PGx lines and long programming cables.  A value of 4 usually") << endl;
				cout << format("resolves most such issues, but programming takes longer.") << endl;
				cout << endl;
				cout << format("The parameter for this command is a decimal value between 1 and 16 inclusive.") << endl;
				cout << endl;
				cout << format("Syntax Example -l4") << endl;
				break;

			case 'k':
			case 'K':
				cout << format("Displays the checksum of a loaded hexfile. This command must be") << endl;
				cout << format("accompanied by the Hex File Selection command (-f).") << endl;
				cout << endl;
				cout << format("This command takes no parameters.") << endl;
				cout << endl;
				cout << format("Syntax Example -k") << endl;
				break;

			case 'm':
			case 'M':
				cout << format("Programs the device with the contents of the loaded hex file.") << endl;
				cout << format("The entire device can be programmed, or just selected memory regions. If one") << endl;
				cout << format("or more selected regions are to be programmed, the program command must be") << endl;
				cout << format("immediately followed by a memory region parameter. Valid parameters are:") << endl;
				cout << format("     P - program memory") << endl;
				cout << format("     E - EEPROM") << endl;
				cout << format("     I - ID Memory") << endl;
				cout << format("     C - Configuration Memory") << endl;
				cout << format("If no memory region parameter is entered, the entire device will be erased and") << endl;
				cout << format("then programmed. Otherwise only the selected memory regions will be programmed") << endl;
				cout << format("without being first erased. Multiple program commands may be entered on one ") << endl;
				cout << format("command line.\"Program Succeeded\" will be displayed if the operation is ") << endl;
				cout << format("successful, otherwise the first address of the first memory region where ") << endl;
				cout << format("programming failed will be displayed along with a description of the failure.") << endl;
				cout << format("NOTE: For HCS and serial EEPROM devices, memory is considered region 'P'") << endl;
				cout << endl;
				cout << format("This command may be used without parameters or with a memory region.") << endl;
				cout << endl;
				cout << format("Syntax Examples -m") << endl;
				cout << format("                -mP -mI -mE") << endl;
				break;

			case 'n':
			case 'N':
				cout << format("Assigns the given string to the PICkit 2 unit as the Unit ID.  The Unit ID is") << endl;
				cout << format("useful in uniquely identifying a PICkit 2 unit.  When multiple PICkit 2 units") << endl;
				cout << format("are connected to a PC, a specific PICkit 2 may be selected using the -S") << endl;
				cout << format("option with the Unit ID. ") << endl;
				cout << endl;
				cout << format("To assign a Unit ID to a PICkit 2, connect only that one unit to the PC and") << endl;
				cout << format("use this option.  To remove a Unit ID, do not include a string after the -N") << endl;
				cout << format("option.  A Unit ID may contain 14 characters maximum.  The Unit ID is stored") << endl;
				cout << format("in non-volatile memory in the PICkit 2 unit itself, and remains assigned") << endl;
				cout << format("changed by a user.") << endl;
				cout << endl;
				cout << format("Syntax Examples -nLab1B   (Set Unit ID = 'Lab1B')") << endl;
				cout << format("                -n        (clear Unit ID)") << endl;
				break;

			case 'p':
			case 'P':
				cout << format("There are three ways to use this option:") << endl;
				cout << format("  1 : -P<part>") << endl;
				cout << format("      Specify the part number of the device explicitly.  This is the") << endl;
				cout << format("      recommended use.  Example: -pPIC16F887") << endl;
				cout << format("  2 : -PF<id>") << endl;
				cout << format("      Auto-Detect a target part connected to PICkit 2 within a given family.") << endl;
                cout << format("      Use '-pf' for a list of auto-detectable families and their family ID") << endl;
				cout << format("      number.  Not all part families support detection.  No programming ") << endl;
				cout << format("      operations are performed when -PF is used without an ID parameter.") << endl;
				cout << format("      Use '-pf<id>' to auto-detect a part within a given family using") << endl;
				cout << format("      the family ID from the listing.  Example: -pf2") << endl;
				cout << format("  3 : -P") << endl;
				cout << format("      Auto-Detect any part in all auto-detectable families when -p is") << endl;
				cout << format("      is used with no parameters.  Example: -p") << endl;
				cout << endl;
				cout << format("The -V and -X options may NOT be used with any form of auto-detect.") << endl;
				cout << format("During auto-detect, VDD is ALWAYS 3.0 Volts unless -W is used.  After a part") << endl;
				cout << format("is detected, the device VDD default or -A voltage is used for remaining") << endl;
                cout << format("operations.") << endl;
				cout << endl;
				cout << format("Auto-detecting can be slower than explicitly specifying the part name.") << endl;
                cout << endl;
				cout << format("WARNING: SOME DEVICE FAMILIES USE A VPP VOLTAGE OF 12 VOLTS ON THE MCLR") << endl;
				cout << format("PIN.  THIS VOLTAGE MAY DAMAGE DEVICES FROM OTHER FAMILIES.  NEVER USE") << endl;
				cout << format("AN AUTODETECT OPTION ON A TARGET WITHOUT A KNOWN GOOD PROGRAMMING") << endl;
				cout << format("CONNECTION.  IT IS SAFER TO AUTO-DETECT WITHIN A GIVEN FAMILY (-PF) THAN") << endl;
				cout << format("WITH ALL DETECTABLE FAMILIES.") << endl;
				cout << endl;
				cout << format("Auto-detecting in all families goes through a special sequence of searching") << endl;
				cout << format("each family to prevent placing damaging voltages on parts.  However, if a") << endl;
				cout << format("programming connection problem prevents a part from being found, it may be") << endl;
				cout << format("exposed to damaging high voltages as other families are searched.") << endl;
				cout << endl;
				cout << format("PK2CMD -?P may be used to list all supported devices and their families.") << endl;
				cout << format("PK2CMD -?P<str> may be used to list only devices matching the search string.") << endl;
				break;

			case 'q':
			case 'Q':
				cout << format("Disables use of a Programming Executive (PE) for PIC24 or dsPIC33 devices.") << endl;
				cout << format("Low-level ICSP is used instead (as in prior versions of PK2CMD).") << endl;
				cout << endl;
				cout << format("Using the PE results in much faster programming operations, and implements") << endl;
				cout << format("the Device ID Corruption workaround for PIC24H/dsPIC33 devices.  However,") << endl;
				cout << format("Blank Check, Programming, and Verify operations will not provide the address") << endl;
				cout << format("and data for failing locations for PIC24H/dsPIC33 as the PE only returns a") << endl;
				cout << format("Good/Bad response.  Disable the PE for address and data information.") << endl;
				cout << endl;
				cout << format("The Programming Executive (PE) for PIC24H and dsPIC33F parts may fail on") << endl;
				cout << format("certain programming ports of certain 44-Pin devices. Known problems exist") << endl;
				cout << format("with using the PGC3/PGD3 port on the following devices:") << endl;
				cout << format("PIC24HJ16GP304, PIC24HJ32GP204") << endl;
				cout << format("dsPIC33FJ16GP304, dsPIC33FJ32GP204, dsPIC33FJ16MC304, dsPIC33FJ32MC204") << endl;
				cout << endl;
				cout << format("Syntax Example -q") << endl;
				break;

			case 'r':
			case 'R':
				cout << format("Releases (3-states) the PICkit 2 /MCLR pin after programming operations") << endl;
				cout << format("complete.  If not specified, then /MCLR is asserted (driven low).") << endl;
				cout << endl;
				cout << format("There are no parameters for this command.") << endl;
				cout << endl;
				cout << format("Syntax Example -r") << endl;
				break;

			case 's':
			case 'S':
				cout << format("When more than one PICkit 2 unit is attached to a PC, this option allows") << endl;
				cout << format("a specific unit to be selected using its Unit ID.  The Unit ID is assigned") << endl;
				cout << format("with the -N option.") << endl;
				cout << endl;
				cout << format("When -S is used without an argument, all attached PICkit 2 units and their") << endl;
				cout << format("Unit IDs will be listed.  Units that do not have a Unit ID assigned will") << endl;
				cout << format("show a dash (-) in the Unit ID column.  When -S is used this way, all other") << endl;
				cout << format("options will be ignored.") << endl;
				cout << endl;
				cout << format("A single character argument of '#' may also be used.  This will list all") << endl;
				cout << format("PICkit 2 units with their Unit IDs and Firmware Versions.  NOTE that this") << endl;
				cout << format("is NOT safe to use when another application is already accessing a PICkit 2") << endl;
				cout << format("unit and may corrupt that USB connection. Also, a Unit ID of the single") << endl;
				cout << format("character '#' is not valid, but may used with longer IDs for example '#1'") << endl;
				cout << format("or '#two' are valid.") << endl;
				cout << endl;
				cout << format("To select a specific unit from among the attached PICkit 2 units to execute") << endl;
				cout << format("a given set of command options, follow the -S option with the Unit ID string") << endl;
				cout << format("of the intended PICkit 2 unit.") << endl;
				cout << endl;
				cout << format("This command may be used with or without a parameter.") << endl;
				cout << endl;
				cout << format("Syntax Example -s        (list connected PICkit 2 units - SAFE)") << endl;
				cout << format("               -s#       (list connected units with FW versions - UNSAFE)") << endl;
				cout << format("               -sLab1B   (use the PICkit 2 with Unit ID string 'Lab1B')") << endl;
				cout << format("               -#3       (use the PICkit 2 with Unit ID string '#3')") << endl;
				break;

			case 't':
			case 'T':
				cout << format("Enables the Vdd output pin after programming operations are complete.") << endl;
				cout << format("If not specified, then Vdd is turned off.  Use -a<> to set the voltage.") << endl;
				cout << endl;
				cout << format("There are no parameters for this command.") << endl;
				cout << endl;
				cout << format("Syntax Example -t") << endl;
				break;

			case 'u':
			case 'U':
				cout << format("Specifies a new OSCCAL value in hex. Used with a Program command, the device") << endl;
				cout << format("will be programmed with this new value. No error checking is done on the value.") << endl;
				cout << endl;
				cout << format("Syntax Example /uC80 or /u0x347C") << endl;
				break;

			case 'v':
			case 'V':
				cout << format("Specifies the Vpp value, in volts, that the device will be programmed with.") << endl;
				cout << format("If not entered, the default value for the device is used.  Normally this") << endl;
				cout << format("value should not be specified.") << endl;
				cout << endl;
				cout << format("The parameter for this command is the floating point value of the desired") << endl;
				cout << format("Vpp voltage.") << endl;
				cout << endl;
				cout << format("Syntax Example -v13.00") << endl;
				break;

			case 'w':
			case 'W':
				cout << format("If this switch is included, the target circuit will not be powered by the ") << endl;
				cout << format("programmer and should be powered by an external power source. If this switch") << endl;
				cout << format("is not included, the target circuit will be powered by the programmer. The") << endl;
				cout << format("PICkit 2 is limited to an external power source voltage range of 2.5 Volts") << endl;
				cout << format("to 5.0 Volts.") << endl;
				cout << endl;
				cout << format("There are no parameters for this command.") << endl;
				cout << endl;
				cout << format("Syntax Example -w") << endl;
				break;

			case 'x':
			case 'X':
				cout << format("If this switch is included, PICkit 2 will attempt to program the device ") << endl;
				cout << format("using the VPP first program entry method.  Not all families and devices") << endl;
				cout << format("support this feature.") << endl;
				cout << endl;
				cout << format("There are no parameters for this command.") << endl;
				cout << endl;
				cout << format("Syntax Example -x") << endl;

			case 'y':
			case 'Y':
				cout << format("Verifies the device against the selected hex file on the programmer.") << endl;
				cout << format("The entire device can be verified, or just selected memory regions. If one") << endl;
				cout << format("or more selected regions are to be verified, the verify command must be") << endl;
				cout << format("immediately followed by a memory region parameter. Valid parameters are:") << endl;
				cout << format("     P - program memory") << endl;
				cout << format("     E - EEPROM") << endl;
				cout << format("     I - ID Memory") << endl;
				cout << format("     C - Configuration Memory") << endl;
				cout << format("If no memory region parameter is entered, the entire device will be verified,") << endl;
				cout << format("otherwise only the selected memory regions will be verified. Multiple verify") << endl;
				cout << format("commands may be entered on one command line. \"Verify Succeeded\" will be") << endl;
				cout << format("displayed if the operation is successful, otherwise the first address of the") << endl;
				cout << format("first memory region where verification failed will be displayed along with a") << endl;
				cout << format("description of the failure.") << endl;
				cout << format("NOTE: For HCS and serial EEPROM devices, memory is considered region 'P'") << endl;
				cout << endl;
				cout << format("This command may be used without parameters or with a memory region.") << endl;
				cout << endl;
				cout << format("Syntax Examples -y") << endl;
				cout << format("                -yP -yI -yE") << endl;
				break;

			case 'z':
			case 'Z':
				cout << format("If this switch is included, then a complete device programming operation (-m)") << endl;
				cout << format("will preserve and not overwrite the existing EEPROM data memory on the device") << endl;
				cout << endl;
				cout << format("There are no parameters for this command.") << endl;
				cout << endl;
				cout << format("Syntax Example -z") << endl;
				break;

			default:
				return false; // may be one that needs the device file.

		}
	}
	else
	{ // just display main help
		displayHelp();
	}

	return true;
}

void Ccmd_app::displayHelp(void)
{
	cout << format("                        PICkit 2 COMMAND LINE HELP") << endl;
	cout << format("Options              Description                              Default") << endl;
	cout << format("----------------------------------------------------------------------------") << endl;
    cout << format("A<value>             Set Vdd voltage                          Device Specific") << endl;
	cout << format("B<path>              Specify the path to " DEVICE_FILE_NAME "    Searches PATH") << endl;
	cout << format("                                                              and calling dir") << endl;
    cout << format("C                    Blank Check Device                       No Blank Check") << endl;
    cout << format("D<file>              OS Download                              None") << endl;
    cout << format("E                    Erase Flash Device                       Do Not Erase") << endl;
    cout << format("F<file>              Hex File Selection                       None") << endl;
    cout << format("G<Type><range/path>  Read functions                           None") << endl;
    cout << format("                     Type F: = read into hex file,") << endl;
    cout << format("                             path = full file path,") << endl;
    cout << format("                             range is not used") << endl;
    cout << format("                     Types P,E,I,C: = ouput read of Program,") << endl;
    cout << format("                             EEPROM, ID and/or Configuration") << endl;
    cout << format("                             Memory to the screen. P and E") << endl;
    cout << format("                             must be followed by an address") << endl;
    cout << format("                             range in the form of x-y where") << endl;
    cout << format("                             x is the start address and y is") << endl;
    cout << format("                             the end address both in hex,") << endl;
    cout << format("                             path is not used") << endl;
    cout << format("                             (Serial EEPROM memory is 'P')") << endl;
    cout << format("H<value>             Delay before Exit                        Exit immediately") << endl;
    cout << format("                         K = Wait on keypress before exit") << endl;
    cout << format("                         1 to 9 = Wait <value> seconds") << endl;
    cout << format("                                  before exit") << endl;
    cout << format("I                    Display Device ID & silicon revision     Do Not Display") << endl;
	cout << format("J<newlines>          Display operation percent complete       Rotating slash") << endl;
	cout << format("                         N = Each update on newline") << endl;
    cout << format("K                    Display Hex File Checksum                Do Not Display") << endl;
    cout << format("L<rate>              Set programming speed                    Fastest") << endl;
    cout << format("                     <rate> is a value of 1-16, with 1 being") << endl;
	cout << format("                     the fastest.") << endl;
    cout << format("M<memory region>     Program Device                           Do Not Program") << endl;
    cout << format("                     memory regions:") << endl;
    cout << format("                         P = Program memory") << endl;
    cout << format("                         E = EEPROM") << endl;
    cout << format("                         I = ID memory") << endl;
    cout << format("                         C = Configuration memory") << endl;
    cout << format("                         If no region is entered, the entire") << endl;
    cout << format("                         device will be erased & programmed.") << endl;
    cout << format("                         If a region is entered, no erase") << endl;
    cout << format("                         is performed and only the given") << endl;
    cout << format("                         region is programmed.") << endl;
    cout << format("                         All programmed regions are verified.") << endl;
    cout << format("			            (serial EEPROM memory is 'P')") << endl;
    cout << format("N<string>            Assign Unit ID string to first found     None") << endl;
    cout << format("                     PICkit 2 unit.  String is limited to 14") << endl;
    cout << format("                     characters maximum.  May not be used") << endl;
    cout << format("                     with other options.") << endl;
    cout << format("                     Example: -NLab1B") << endl;
    cout << format("P<part>              Part Selection. Example: -PPIC16f887     (Required)") << endl;
	cout << format("P                    Auto-Detect in all detectable families") << endl;
	cout << format("PF                   List auto-detectable part families") << endl;
	cout << format("PF<id>               Auto-Detect only within the given part") << endl;
	cout << format("                     family, using the ID listed with -PF") << endl;
	cout << format("                     Example: -PF2") << endl;
	cout << format("Q                    Disable PE for PIC24/dsPIC33 devices     Use PE") << endl;
    cout << format("R                    Release /MCLR after operations           Assert /MCLR") << endl;
    cout << format("S<string/#>          Use the PICkit 2 with the given Unit ID  First found unit") << endl;
    cout << format("                     string.  Useful when multiple PICkit 2") << endl;
    cout << format("                     units are connected.") << endl;
    cout << format("                     Example: -SLab1B") << endl;
    cout << format("                     If no <string> is entered, then the") << endl;
    cout << format("                     Unit IDs of all connected units will be") << endl;
    cout << format("                     displayed.  In this case, all other ") << endl;
    cout << format("                     options are ignored. -S# will list units") << endl;
	cout << format("                     with their firmware versions.") << endl;
	cout << format("                     See help -s? for more info.") << endl;
    cout << format("T                    Power Target after operations            Vdd off") << endl;
    cout << format("U<value>             Program OSCCAL memory, where:            Do Not Program") << endl;
    cout << format("                      <value> is a hexadecimal number") << endl;
    cout << format("                      representing the OSCCAL value to be") << endl;
    cout << format("                      programmed. This may only be used in") << endl;
    cout << format("                      conjunction with a programming ") << endl;
    cout << format("                      operation.") << endl;
    cout << format("V<value>             Vpp override                             Device Specific") << endl;
    cout << format("W                    Externally power target                  Power from Pk2") << endl;
	cout << format("X                    Use VPP first Program Entry Method       VDD first") << endl;
    cout << format("Y<memory region>     Verify Device                            Do Not Verify") << endl;
    cout << format("                         P = Program memory") << endl;
    cout << format("                         E = EEPROM") << endl;
    cout << format("                         I = ID memory") << endl;
    cout << format("                         C = Configuration memory") << endl;
    cout << format("                         If no region is entered, the entire") << endl;
    cout << format("                         device will be verified.") << endl;
    cout << format("                         (Serial EEPROM memory is 'P')") << endl;
    cout << format("Z                    Preserve EEData on Program               Do Not Preserve") << endl;
    cout << format("?                    Help Screen                              Not Shown") << endl;
    cout << endl;
    cout << format("     Each option must be immediately preceeded by a switch, Which can") << endl;
    cout << format("     be either a dash <-> or a slash </> and options must be separated") << endl;
    cout << format("     by a single space.") << endl;
    cout << endl;
    cout << format("     Example:   PK2CMD /PPIC16F887 /Fc:\\mycode /M") << endl;
    cout << format("                               or") << endl;
    cout << format("                PK2CMD -PPIC16F887 -Fc:\\mycode -M") << endl;
    cout << endl;
    cout << format("     Any option immediately followed by a question mark will invoke") << endl;
    cout << format("     a more detailed description of how to use that option.") << endl;
    cout << endl;
    cout << format("     Commands and their parameters are not case sensitive. Commands will") << endl;
    cout << format("     be processed according to command order of precedence, not the order") << endl;
    cout << format("     in which they appear on the command line. ") << endl;
    cout << format("	Precedence:") << endl;
    cout << format("                -?      (first)") << endl;
	cout << format("                -B") << endl;
	cout << format("                -S") << endl;
    cout << format("                -D") << endl;
	cout << format("                -N") << endl;
    cout << format("                -P") << endl;
    cout << format("                -A -F -J -L -Q -V -W -X -Z") << endl;
    cout << format("                -C") << endl;
    cout << format("                -U") << endl;
    cout << format("                -E") << endl;
    cout << format("                -M") << endl;
    cout << format("                -Y") << endl;
    cout << format("                -G") << endl;
    cout << format("                -I -K") << endl;
    cout << format("                -R -T") << endl;
    cout << format("                -H      (last)") << endl;
    cout << format("		") << endl;
    cout << format("     The program will return an exit code upon completion which will") << endl;
    cout << format("     indicate either successful completion, or describe the reason for") << endl;
    cout << format("     failure. To view the list of exit codes and their descriptions,") << endl;
    cout << format("     type -?E on the command line.") << endl;
	cout << endl;
	cout << format("     type -?V on the command line for version information.") << endl;
	cout << endl;
	cout << format("     type -?L on the command line for license information.") << endl;
	cout << endl;
	cout << format("     type -?P on the command line for a listing of supported devices.") << endl;
	cout << format("     type -?P<string> to search for and display a list of supported devices") << endl;
	cout << format("                      beginning with <string>.") << endl;
	cout << endl;
	cout << format("     Special thanks to the following individuals for their critical") << endl;
	cout << format("     contributions to the development of this software:") << endl;
	cout << format("		Jeff Post, Xiaofan Chen, and Shigenobu Kimura") << endl;
}

bool Ccmd_app::checkHelp2(TextVec& args, bool loadDeviceFileFailed)
{  // helps that need the device file loaded
	int i;

	_TCHAR** parg = NULL;

	// look for '?' in all arguments.  Display help for first found
	for (i = 0; i < NSIZE(args); i++)
	{
		parg = &args[i];

		if (TXT_SEEK_TCHAR(*parg, '?'))
			break;
	}
	
	if (i == NSIZE(args)) // none found
		return false;

	if (checkSwitch(*parg))
	{
		switch ((*parg)[1])
		{
			case '?':
				if (TXT_LENGTH(*parg) > 2)
				{
					if (((*parg)[2] == 'v') || ((*parg)[2] == 'V'))
					{
						cout << endl << format("Executable Version:    %d.%02d.%02d", VERSION_MAJOR, VERSION_MINOR, VERSION_DOT);
						
						if (loadDeviceFileFailed)
							cout << endl << "Device File Version:   not found" << endl;
						else
							cout << endl << format("Device File Version:   %d.%02d.%02d", PicFuncs.DevFile.Info.VersionMajor,
								PicFuncs.DevFile.Info.VersionMinor, PicFuncs.DevFile.Info.VersionDot) << endl;
						// Look for PICkit 2
						selectUnitArg(args);
						if (PicFuncs.DetectPICkit2Device(pk2UnitIndex, true))
						{
							cout << format("OS Firmware Version:   %d.%02d.%02d", PicFuncs.FirmwareVersion.major,
								PicFuncs.FirmwareVersion.minor, PicFuncs.FirmwareVersion.dot) << endl << endl;
						}
						else
						{
							cout << "OS Firmware Version:   PICkit 2 not found" << endl << endl;
						}

					}
					else if (((*parg)[2] == 'p') || ((*parg)[2] == 'P'))
					{
						if (loadDeviceFileFailed)
						{
							cout << "Unable to list parts: Device File Load Failed" << endl << endl;
						}
						else
						{
							_TCHAR searchTerm[MAX_PATH];
							// get search term
							XRIGHTCOPY(searchTerm,*parg,3);
							*parg = (char *) "";
							int j = 1;
							while (((i+j) < NSIZE(args)) && (!checkSwitch(args[i+j])))
							{ // check for term with space(s) in it
								TXT_PUSH_UNSAFE(searchTerm, args[i+j]);
								args[i + j++] = (char *) "";
							}
							displayPartList(args, searchTerm);
						}
					}
					else
					{
						displayHelp();
					}
				}
				break;

			default:
				cout << format("Invalid command, or no Help available to for specified command.") << endl;

		}
	}

	return true;
}

void Ccmd_app::displayLicense(void)
{
	cout << format("IMPORTANT: ") << endl;
	cout << format("YOU MUST ACCEPT THE TERMS AND CONDITIONS OF THIS LICENSE AGREEMENT") << endl;
	cout << format("TO RECEIVE A LICENSE FOR THE ACCOMPANYING SOFTWARE.  TO ACCEPT THE") << endl;
    cout << format("TERMS OF THIS LICENSE, OPEN THIS PACKAGE AND PROCEED WITH THE") << endl;
	cout << format("DOWNLOAD OR USE OF THE SOFTWARE.  IF YOU DO NOT ACCEPT THESE LICENSE") << endl;
	cout << format("TERMS, DO NOT OPEN THIS PACKAGE, DOWNLOAD, OR USE THIS SOFTWARE.") << endl;
	cout << endl;
	cout << format("PICkit(tm) 2 PK2CMD SOFTWARE LICENSE ") << endl;
	cout << endl;
	cout << format("This License Agreement (Agreement) is a contract between You (as") << endl;
	cout << format("an individual or as a representative of your employer) and") << endl;
	cout << format("Microchip Technology Incorporated (\"Company\") for the PICkit(tm) 2") << endl;
	cout << format("PK2CMD software (including source code) accompanying this Agreement") << endl;
	cout << format("(the \"Software\").  In consideration for access to the Software, You") << endl;
	cout << format("agree to be bound by this Agreement. ") << endl;
	cout << endl;
	cout << format("1.  LICENSE GRANT. Subject to all of the terms and conditions of") << endl;
	cout << format("this Agreement, Company grants You a non-exclusive, non-") << endl;
	cout << format("sublicensable, non-transferable license to use the Software with") << endl;
	cout << format("Company products, modify the Software for use with Company products,") << endl;
	cout << format("and market, sell or otherwise distribute: ") << endl;
	cout << endl;
	cout << format("(a) Your end application that integrates Software and Company") << endl;
	cout << format("    products (\"Licensee Product\"); or ") << endl;
	cout << endl;
	cout << format("(b) Your modifications to the Software provided that the modified") << endl;
	cout << format("    Software has the following copyright and disclaimer notice") << endl;
	cout << format("    prominently posted in a location where end users will see it") << endl;
	cout << format("    (e.g., installation program, program headers, About Box, etc.):") << endl;
	cout << endl;
	cout << format("\"Copyright (c) 2005-2009 Microchip Technology Inc. All rights") << endl;
	cout << format("reserved. This version of the PICkit(tm) 2 PK2CMD Software has been") << endl;
	cout << format("modified by [INSERT YOUR NAME, DATE OF SOFTWARE MODIFICATION HERE].") << endl;
	cout << format("You may use, copy, modify and distribute the Software for use with") << endl;
	cout << format("Microchip products only.  If you distribute the Software or its") << endl;
	cout << format("derivatives, the Software must have this copyright and disclaimer") << endl;
	cout << format("notice prominently posted in a location where end users will see it") << endl;
	cout << format("(e.g., installation program, program headers, About Box, etc.).  To") << endl;
	cout << format("the maximum extent permitted by law, this Software is distributed") << endl;
	cout << format("\"AS IS\" and WITHOUT ANY WARRANTY INCLUDING BUT NOT LIMITED TO ANY") << endl;
	cout << format("IMPLIED WARRANTY OF MERCHANTABILITY, FITNESS FOR PARTICULAR PURPOSE,") << endl;
	cout << format("or NON-INFRINGEMENT. IN NO EVENT WILL MICROCHIP OR ITS LICENSORS BE") << endl;
	cout << format("LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL") << endl;
	cout << format("DAMAGESOF ANY KIND ARISING FROM OR RELATED TO THE USE, MODIFICATION") << endl;
	cout << format("OR DISTRIBUTION OF THIS SOFTWARE OR ITS DERIVATIVES.\"" "") << endl;
	cout << endl;
	cout << format("You may not copy or reproduce all or any portion of Software, except") << endl;
	cout << format("to the extent that such activity is specifically allowed by this") << endl;
	cout << format("Agreement or expressly permitted by applicable law notwithstanding") << endl;
	cout << format("the foregoing limitations.") << endl;
	cout << endl;
	cout << format("All copies of the Software created by You or for You, including") << endl;
	cout << format("derivatives, must include the copyright, trademark and other") << endl;
	cout << format("proprietary notices as they appear on the original or, in the event") << endl;
	cout << format("You modified the Software, the notice listed above. You may not") << endl;
	cout << format("remove or alter any identifying screen that is produced by the") << endl;
	cout << format("Software.") << endl;
	cout << endl;
	cout << format("2.  OWNERSHIP AND TITLE. Software is licensed pursuant to the") << endl;
	cout << format("    Agreement, not sold.  All right, title and interest, including") << endl;
	cout << format("    intellectual property rights, in and to Software, derivatives") << endl;
	cout << format("    thereof, implementation of the Software in microcontrollers,") << endl;
	cout << format("    and hardware and software implementations of Software or") << endl;
	cout << format("    derivatives shall remain in Company. You will not obtain") << endl;
	cout << format("    ownership rights to derivatives of Software, and by accepting") << endl;
	cout << format("    the terms of this Agreement assign any such rights to Company") << endl;
	cout << format("    that You do receive.  Except as specifically stated in the") << endl;
	cout << format("    Agreement, you are granted no other rights, express or implied,") << endl;
	cout << format("    to the Software, derivatives thereof, or other Company") << endl;
	cout << format("    intellectual property such as trade secrets, patents, ") << endl;
	cout << format("    copyrights, and trademarks.") << endl;
	cout << endl;
	cout << format("3.  CONFIDENTIALITY. You agree not to disclose Software to any") << endl;
	cout << format("    third party, except as permitted by this Agreement.  To the") << endl;
	cout << format("    extent that Software becomes part of the public domain, is") << endl;
	cout << format("    independently developed, or obtained free from any obligation") << endl;
	cout << format("    of confidentiality then the obligation of confidentiality") << endl;
	cout << format("    under this Agreement shall not apply.") << endl;
 	cout << endl;
	cout << format("4.  COPYRIGHT. The Software is protected by U.S. copyright laws") << endl;
	cout << format("    and international copyright treaties, as well as other") << endl;
	cout << format("    intellectual property laws and treaties.") << endl;
	cout << endl;
	cout << format("5.  TERMINATION OF AGREEMENT. Without prejudice to any other") << endl;
	cout << format("    rights, Company may terminate this Agreement if You fail to") << endl;
	cout << format("    comply with the terms and conditions of this Agreement.") << endl;
	cout << format("    Upon termination, You shall immediately: (a) stop using and") << endl;
	cout << format("    distributing the Software and derivatives thereof; (b) destroy") << endl;
	cout << format("    all copies of the Software and derivatives in your possession;") << endl;
	cout << format("    and (c) remove Software from any of Your tangible media and") << endl;
	cout << format("    from systems on which the Software exists.  Termination of") << endl;
	cout << format("    this License shall not affect the right of any end user or") << endl;
	cout << format("    consumer to use Licensee Product or modified Software;") << endl;
	cout << format("    provided that such product or modified Software was purchased") << endl;
	cout << format("    or distributed prior to the termination of this License.") << endl;
	cout << endl;
	cout << format("6.  DANGEROUS APPLICATIONS. You acknowledge that Software has not") << endl;
	cout << format("    been designed to be fault tolerant.  You warrant that You will") << endl;
	cout << format("    not use Software or derivatives in a dangerous, hazardous, or") << endl;
	cout << format("    life supporting application where the failure of such") << endl;
	cout << format("    application could lead directly to death, personal injury, or") << endl;
	cout << format("    environmental damage.") << endl;
	cout << endl;
	cout << format("7.  INDEMNITY. You will indemnify and hold Company and its") << endl;
	cout << format("    licensor(s), its related companies and its suppliers, harmless") << endl;
	cout << format("    for, from and against, any claims, costs (including attorney's") << endl;
	cout << format("    fees), damages or liabilities, including without limitation") << endl;
	cout << format("    product liability claims, arising out of: (a) Your use,") << endl;
	cout << format("    modification and distribution of the Software and its") << endl;
	cout << format("    derivatives; or (b) violation of this Agreement. COMPANY AND") << endl;
	cout << format("    ITS LICENSOR(S) ASSUME NO RESPONSIBILITY FOR, NOR INDEMNIFY") << endl;
	cout << format("    YOU AGAINST, ANY PATENT, COPYRIGHT OR OTHER INTELLECTUAL") << endl;
	cout << format("    PROPERTY CLAIMS BROUGHT AGAINST YOU RELATING TO THE SOFTWARE.") << endl;
	cout << endl;
	cout << format("8.  NO WARRANTY. TO THE MAXIMUM EXTENT PERMITTED BY LAW, COMPANY") << endl;
	cout << format("    AND ITS LICENSOR PROVIDE SOFTWARE \"AS IS\" AND EXPRESSLY") << endl;
	cout << format("    DISCLAIM ANY WARRANTY OF ANY KIND, WHETHER EXPRESS OR IMPLIED,") << endl;
	cout << format("    INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF") << endl;
	cout << format("    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR") << endl;
	cout << format("    NON-INFRINGEMENT. YOU ASSUME THE ENTIRE RISK ARISING OUT OF") << endl;
	cout << format("    USE OR PERFORMANCE OF SOFTWARE, AS WELL AS ANY DERIVATIVES OF") << endl;
	cout << format("    THE SOFTWARE MADE FOR YOU OR ON YOUR BEHALF.  COMPANY AND ITS") << endl;
	cout << format("    LICENSOR(S) ASSUME NO RESPONSIBILITY FOR THE ACCURACY OR") << endl;
	cout << format("    ERRORS OR OMISSIONS OF SOFTWARE AND DO NOT WARRANT THE") << endl;
	cout << format("    FOLLOWING: (A) THE FUNCTIONS CONTAINED IN SOFTWARE WILL MEET") << endl;
	cout << format("    YOUR REQUIREMENTS; (B) THE OPERATION OF SOFTWARE WILL BE") << endl;
	cout << format("    UNINTERRUPTED OR ERROR-FREE; OR (C) ANY DEFECTS IN SOFTWARE") << endl;
	cout << format("    WILL BE CORRECTED. ") << endl;
	cout << endl;
	cout << format("9.  LIMITATION OF LIABILITY. COMPANY AND ITS LICENSOR TOTAL") << endl;
	cout << format("    AGGREGATE LIABILITY IN CONTRACT, WARRANTY, TORT (INCLUDING") << endl;
	cout << format("    NEGLIGENCE OR BREACH OF STATUTORY DUTY), STRICT LIABILITY,") << endl;
	cout << format("    INDEMNITY, CONTRIBUTION, OR OTHERWISE, SHALL NOT EXCEED THE") << endl;
	cout << format("    LICENSE FEE YOU PAID FOR THE SOFTWARE. IN NO EVENT SHALL") << endl;
	cout << format("    COMPANY AND ITS LICENSOR BE LIABLE FOR ANY INCIDENTAL, SPECIAL,") << endl;
	cout << format("    INDIRECT OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA,") << endl;
	cout << format("    HARM TO YOUR EQUIPMENT, COST OF PROCUREMENT OF SUBSTITUTE") << endl;
	cout << format("    GOODS, TECHNOLOGY OR SERVICES, ANY CLAIMS BY THIRD PARTIES") << endl;
	cout << format("    (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), ANY CLAIMS") << endl;
	cout << format("    FOR INDEMNITY OR CONTRIBUTION, OR OTHER SIMILAR COSTS. If any") << endl;
	cout << format("    remedy is determined to have failed of its essential purpose,") << endl;
	cout << format("    all limitations of liability and exclusion of damages set forth") << endl;
	cout << format("    in the limited warranty shall remain in effect.") << endl;
	cout << endl;
	cout << format("10. SURVIVAL.  Sections 2-15 shall survive termination of this") << endl;
	cout << format("     Agreement. ") << endl;
	cout << endl;
	cout << format("11. CHOICE OF LAW; VENUE; LIMITATIONS ON CLAIMS. You agree that") << endl;
	cout << format("    this Agreement and any conflicts regarding Software, shall be") << endl;
	cout << format("    construed, interpreted and governed by the laws, and subject") << endl;
	cout << format("    to the exclusive jurisdiction of the state or territory in the") << endl;
	cout << format("    Company Terms and Conditions of Sale (\"T&Cs\").  In the event") << endl;
	cout << format("    that the T&Cs do not apply or the choice of law or") << endl;
	cout << format("    jurisdiction are not indicated therein, then this Agreement") << endl;
	cout << format("    shall be construed, interpreted and governed by the laws, and") << endl;
	cout << format("    subject to the exclusive jurisdiction of the State of Arizona,") << endl;
	cout << format("    U.S.A. without regard to any conflict of laws principles. You") << endl;
	cout << format("    agree that regardless of any law to the contrary, any cause of") << endl;
	cout << format("    action related to or arising out of this Agreement or Software") << endl;
	cout << format("    must be filed within one year after such cause of action") << endl;
	cout << format("    arose, or be considered waived.") << endl;
	cout << endl;
	cout << format("12. EXPORT COMPLIANCE. You will not export or re-export Software,") << endl;
	cout << format("    technical data, direct products thereof or any other items") << endl;
	cout << format("    which would violate any applicable export control laws and") << endl;
	cout << format("    regulations including, but not limited to, those of the United") << endl;
	cout << format("    States and the United Kingdom.  You agree that it is Your") << endl;
	cout << format("    responsibility to obtain copies of and to familiarize yourself") << endl;
	cout << format("    fully with these laws and regulations to avoid violation.") << endl;
	cout << endl;
	cout << format("13. ASSIGNMENT. Neither this agreement nor any rights, licenses") << endl;
	cout << format("    or obligations hereunder, may be assigned by You without the") << endl;
	cout << format("    Company's prior written approval.") << endl;
	cout << endl;
	cout << format("14. ENTIRE AGREEMENT: MODIFICATIONS AND WAIVER. This Agreement") << endl;
	cout << format("    constitutes the entire agreement of the parties with respect") << endl;
	cout << format("    to the subject matter of this Agreement, and merges and") << endl;
	cout << format("    supersedes all communications relating to this subject matter,") << endl;
	cout << format("    whether written or oral. Except as expressly set forth in this") << endl;
	cout << format("    Agreement, no modification of this Agreement will be effective") << endl;
	cout << format("    unless made in writing signed by Company.  No failure or delay") << endl;
	cout << format("    by Company or its licensor(s) to assert any rights or remedies") << endl;
	cout << format("    arising from a breach of this Agreement shall be construed as a") << endl;
	cout << format("    waiver or a continuing waiver of such rights and remedies, nor") << endl;
	cout << format("    shall failure or delay to assert a breach be deemed to waive that") << endl;
	cout << format("    or any other breach. If any part of this Agreement is found by a") << endl;
	cout << format("    court of competent jurisdiction to be invalid, unlawful or") << endl;
	cout << format("    unenforceable then such part shall be severed from the remainder") << endl;
	cout << format("    of this Agreement and replaced with a valid provision that comes") << endl;
	cout << format("    closest to the intention underlying the invalid provision.") << endl;
	cout << endl;
	cout << format("Copyright (c) 2005-2009, Microchip Technology Inc. All rights") << endl;
	cout << format("reserved. ") << endl;

}

void Ccmd_app::displayPartList(TextVec& args, _TCHAR* argSearch)
{
	_TCHAR *partlist_array[1024];
	int partNum, partIdx;

	string2Upper(argSearch, MAX_PATH);

	cout << endl << format("Device File Version:   %d.%02d.%02d", PicFuncs.DevFile.Info.VersionMajor,
		PicFuncs.DevFile.Info.VersionMinor, PicFuncs.DevFile.Info.VersionDot) << endl;

	// display sorted parts by family, in family display order
	if (argSearch[0] == 0)
		cout << format("Number of devices = %i", PicFuncs.DevFile.Info.NumberParts - 1) << endl << endl; // don't count "unsupported" device
	else
		cout << format("List of devices starting with '%s':", argSearch) << endl << endl;
	cout << "Device Name                  Device Family" << endl;
	cout << "-----------                  -------------" << endl;
	for (int index = 0; index < PicFuncs.DevFile.Info.NumberFamilies ; index++)
	{
		for (int order = 0; order < PicFuncs.DevFile.Info.NumberFamilies; order++)
		{
			if (PicFuncs.DevFile.Families[order].FamilyType == index)
			{
				// get all the parts in this family
				partNum = 0;
				for (partIdx = 1; partIdx < (PicFuncs.DevFile.Info.NumberParts); partIdx++)
				{
					// skip first part, which is "unsupported part"
					if (PicFuncs.DevFile.PartsList[partIdx].Family == order)
						partlist_array[partNum++] = strdup(PicFuncs.DevFile.PartsList[partIdx].PartName);
				}
				// sort them
				qsort(partlist_array, partNum, sizeof(_TCHAR *), strnatcmpWrapper);
				// list them
				if (argSearch[0] == 0)
				{ // list all parts
					for (partIdx = 0; partIdx < partNum; partIdx++)
					{
						cout << format("%-28s %s", partlist_array[partIdx], PicFuncs.DevFile.Families[order].FamilyName) << endl;
					}
				}
				else
				{ // search parts
					int l = TXT_LENGTH(argSearch);
					for (partIdx = 0; partIdx < partNum; partIdx++)
					{
						if (TXT_COMPARE(partlist_array[partIdx], argSearch, l) == 0)
							cout << format("%-28s %s", partlist_array[partIdx], PicFuncs.DevFile.Families[order].FamilyName) << endl;
					}
				}
			}
		}
	}

	cout << endl << format("NOTE: If the name contains a space with additional info in parentheses, Ex:") << endl;
	cout << format("        PIC16F636 (639)") << endl << format("        93LC46A (C X8)") << endl;
	cout << format("      then only the characters before the space are required for -P, Ex:") << endl;
	cout << format("        -pPIC16F636") << endl << format("        -p93LC46A") << endl;
}

int Ccmd_app::strnatcmpWrapper(const void *a, const void *b)
{
	return strnatcmp(*(char const **)a, *(char const **)b);
}
