/*
Copyright 2023 The Foedag team

GPL License

Copyright (c) 2023 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Programmer.h"

#include <iostream>
#include <numeric>
#include <sstream>  // for std::stringstream
#include <thread>
#include <unordered_set>

#include "CFGCommon/CFGArg_auto.h"
#include "CFGCommon/CFGCommon.h"
#include "ProgrammerGuiInterface.h"
#include "Programmer_helper.h"
#include "libusb.h"
#include "tcl.h"
namespace FOEDAG {

// openOCDPath used by library
static std::string libOpenOcdExecPath;
static std::vector<TapInfo> foundTap;
static std::map<std::string, Cable> cableMap;
static bool isCableMapInitialized = false;
static bool isHwDbInitialized = false;
static std::vector<HwDevices> cableDeviceDb;

std::map<int, std::string> ErrorMessages = {
    {NoError, "Success"},
    {InvalidArgument, "Invalid argument"},
    {DeviceNotFound, "Device not found"},
    {CableNotFound, "Cable not found"},
    {CableNotSupported, "Cable not supported"},
    {NoSupportedTapFound, "No supported tap found"},
    {FailedExecuteCommand, "Failed to execute command"},
    {FailedToParseOutput, "Failed to parse output"},
    {BitfileNotFound, "Bitfile not found"},
    {FailedToProgramFPGA, "Failed to program FPGA"},
    {OpenOCDExecutableNotFound, "OpenOCD executable not found"},
    {FailedToProgramOTP, "Failed to program device OTP"},
    {InvalidFlashSize, "Invalid flash size"},
    {UnsupportedFunc, "Unsupported function"}};

void programmer_entry(CFGCommon_ARG* cmdarg) {
  auto arg = std::static_pointer_cast<CFGArg_PROGRAMMER>(cmdarg->arg);
  if (arg == nullptr) return;

  if (arg->m_help) {
    return;
  }

  std::string subCmd = arg->get_sub_arg_name();
  if (cmdarg->compilerName == "dummy") {
    Cable cable1{};
    cable1.index = 1;
    cable1.name = "UsbProgrammerCable_1_1";
    Cable cable2{};
    cable2.name = "UsbProgrammerCable_1_2";
    cable2.index = 2;
    Device device1{};
    Device device2{};
    device1.name = device2.name = "Gemini";
    device1.index = 1;
    device2.index = 2;
    device1.flashSize = device2.flashSize = 16384;

    if (subCmd == "list_device") {
      auto list_device =
          static_cast<const CFGArg_PROGRAMMER_LIST_DEVICE*>(arg->get_sub_arg());
      processDeviceList(cable1, {device1, device2}, list_device->verbose);
      cmdarg->tclOutput =
          "UsbProgrammerCable_1_1-Gemini<1>-16KB "
          "UsbProgrammerCable_1_1-Gemini<2>-16KB";
    } else if (subCmd == "list_cable") {
      auto list_cable_arg =
          static_cast<const CFGArg_PROGRAMMER_LIST_CABLE*>(arg->get_sub_arg());
      processCableList({cable1, cable2}, list_cable_arg->verbose);
      cmdarg->tclOutput = "UsbProgrammerCable_1_1 UsbProgrammerCable_1_2";
    } else if (subCmd == "fpga_status") {
      cmdarg->tclOutput = "1 0";
    } else if (subCmd == "fpga_config") {
      auto fpga_config_arg =
          static_cast<const CFGArg_PROGRAMMER_FPGA_CONFIG*>(arg->get_sub_arg());
      std::string bitstreamFile = fpga_config_arg->m_args[0];
      std::string cableInput = fpga_config_arg->cable;
      uint64_t deviceIndex = fpga_config_arg->index;
      auto device = deviceIndex == 1 ? device1 : device2;
      if (Gui::GuiInterface())
        Gui::GuiInterface()->ProgramFpga(cable1, device, bitstreamFile);
      int status{TCL_OK};
      for (int i = 10; i <= 100; i += 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (Gui::GuiInterface())
          Gui::GuiInterface()->Progress(std::to_string(i));
        if (Gui::GuiInterface() && Gui::GuiInterface()->Stop()) {
          status = TCL_ERROR;
          break;
        }
        CFG_POST_MSG("<test> program fpga - %d %%", i);
      }
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Status(cable1, device, status);
    } else if (subCmd == "otp") {
      auto fpga_config_arg =
          static_cast<const CFGArg_PROGRAMMER_OTP*>(arg->get_sub_arg());
      std::string bitstreamFile = fpga_config_arg->m_args[0];
      std::string cableInput = fpga_config_arg->cable;
      uint64_t deviceIndex = fpga_config_arg->index;
      auto device = deviceIndex == 1 ? device1 : device2;
      if (Gui::GuiInterface())
        Gui::GuiInterface()->ProgramOtp(cable1, device, bitstreamFile);
      int status{TCL_OK};
      for (int i = 10; i <= 100; i += 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (Gui::GuiInterface())
          Gui::GuiInterface()->Progress(std::to_string(i));
        if (Gui::GuiInterface() && Gui::GuiInterface()->Stop()) {
          status = TCL_ERROR;
          break;
        }
        CFG_POST_MSG("<test> program otp - %d %%", i);
      }
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Status(cable1, device, status);
    } else if (subCmd == "flash") {
      auto flash_arg =
          static_cast<const CFGArg_PROGRAMMER_FLASH*>(arg->get_sub_arg());
      std::string bitstreamFile = flash_arg->m_args[0];
      std::string cableInput = flash_arg->cable;
      uint64_t deviceIndex = flash_arg->index;
      auto device = deviceIndex == 1 ? device1 : device2;
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Flash(cable1, device, bitstreamFile);
      auto operations = parseOperationString(flash_arg->operations);
      if (isOperationRequested("erase", operations)) {
        CFG_POST_MSG("<test> Erasing flash memory");
        for (int i = 10; i <= 100; i += 10) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          if (Gui::GuiInterface())
            Gui::GuiInterface()->Progress(std::to_string(i));
          CFG_POST_MSG("<test> erase flash - %d %% ", i);
        }
      }
      if (isOperationRequested("blankcheck", operations)) {
        CFG_POST_MSG("<test> Flash blank check start ...");
        CFG_POST_MSG("<test> Flash blank check complete.");
      }
      int status = TCL_OK;
      if (isOperationRequested("program", operations)) {
        CFG_POST_MSG("<test> Programming flash memory");
        for (int i = 10; i <= 100; i += 10) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          if (Gui::GuiInterface())
            Gui::GuiInterface()->Progress(std::to_string(i));
          if (Gui::GuiInterface() && Gui::GuiInterface()->Stop()) {
            status = TCL_ERROR;
            break;
          }
          CFG_POST_MSG("<test> program flash - %d %% ", i);
        }
        if (Gui::GuiInterface())
          Gui::GuiInterface()->Status(cable1, device, status);
      }
      if (isOperationRequested("verify", operations)) {
        CFG_POST_MSG("<test> Flash verification start ...");
        for (int i = 10; i <= 100; i += 10) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
          CFG_POST_MSG("<test> flash verified- %d %% ", i);
        }
      }
    }
  } else {
    // check openocd executable
    const std::filesystem::path openOcdExecPath = cmdarg->toolPath;
    std::error_code ec;
    if (!std::filesystem::exists(openOcdExecPath, ec)) {
      CFG_POST_ERR("Cannot find openocd executable: %s. %s. ",
                   openOcdExecPath.string().c_str(),
                   (ec ? ec.message().c_str() : ""));
      return;
    }
    int status = 0;
    InitLibrary(openOcdExecPath.string());
    if (subCmd == "list_device") {
      auto list_device =
          static_cast<const CFGArg_PROGRAMMER_LIST_DEVICE*>(arg->get_sub_arg());
      std::vector<Device> devices;
      std::vector<Cable> cables;
      if (list_device->m_args.size() == 1) {
        std::string cableInput = list_device->m_args[0];
        if (!isCableMapInitialized) {
          InitializeCableMap(cables, cableMap);
          isCableMapInitialized = true;
        }
        auto cableIterator = cableMap.find(cableInput);
        if (cableIterator == cableMap.end()) {
          CFG_POST_ERR("Cable not found: %s", list_device->m_args[0].c_str());
          return;
        }
        Cable cable = cableIterator->second;
        status = ListDevices(cable, devices);
        if (status != ProgrammerErrorCode::NoError) {
          CFG_POST_ERR("Failed to list devices. Error code: %d", status);
          return;
        }
        processDeviceList(cable, devices, list_device->verbose);
        if (!devices.empty()) {
          cmdarg->tclOutput =
              buildCableDevicesAliasNameWithSpaceSeparatedString(cable,
                                                                 devices);
        }
      } else {
        InitializeHwDb(cableDeviceDb, cableMap, list_device->verbose,
                       processDeviceList);
        if (!cableDeviceDb.empty()) {
          for (const HwDevices& hwDevice : cableDeviceDb) {
            cmdarg->tclOutput +=
                buildCableDevicesAliasNameWithSpaceSeparatedString(
                    hwDevice.getCable(), hwDevice.getDevices()) +
                " ";
          }
          if (!cmdarg->tclOutput.empty()) {
            cmdarg->tclOutput.pop_back();
          }
        }
        isHwDbInitialized = true;
      }
    } else if (subCmd == "list_cable") {
      auto list_cable_arg =
          static_cast<const CFGArg_PROGRAMMER_LIST_CABLE*>(arg->get_sub_arg());
      std::vector<Cable> cables;
      InitializeCableMap(cables, cableMap);
      isCableMapInitialized = true;
      processCableList(cables, list_cable_arg->verbose);

      if (!cables.empty()) {
        std::string cableNamesTclOuput =
            std::accumulate(cables.begin(), cables.end(), std::string(),
                            [](const std::string& a, const Cable& c) {
                              return a + (a.empty() ? "" : " ") + c.name;
                            });
        cmdarg->tclOutput = cableNamesTclOuput;
      } else {
        cmdarg->tclOutput.clear();
      }
    } else if (subCmd == "fpga_status") {
      auto fpga_status_arg =
          static_cast<const CFGArg_PROGRAMMER_FPGA_STATUS*>(arg->get_sub_arg());
      std::string cableInput = fpga_status_arg->cable;
      uint64_t deviceIndex = fpga_status_arg->index;
      if (deviceIndex == 0) {
        CFG_POST_ERR("Invalid device index: %d", deviceIndex);
        return;
      }
      if (!isHwDbInitialized) {
        InitializeHwDb(cableDeviceDb, cableMap, false);
        isHwDbInitialized = true;
      }
      CfgStatus cfgStatus;
      std::string statusPrintOut;
      auto cableIterator = cableMap.find(cableInput);
      if (cableIterator == cableMap.end()) {
        CFG_POST_ERR("Cable not found: %s", cableInput.c_str());
        return;
      }
      Cable cable = cableIterator->second;
      Device device;
      if (findDeviceFromDb(cableDeviceDb, cable, deviceIndex, device)) {
        status = GetFpgaStatus(cable, device, cfgStatus, statusPrintOut);
        if (status != ProgrammerErrorCode::NoError) {
          CFG_POST_ERR("Failed to get available devices status. Error code: %d",
                       status);
          return;
        }
        if (fpga_status_arg->verbose) {
          CFG_POST_MSG("\n%s", statusPrintOut.c_str());
        }
        cmdarg->tclOutput = std::to_string(cfgStatus.cfgDone) + " " +
                            std::to_string(cfgStatus.cfgError);
      } else {
        CFG_POST_ERR("Device not found: %d", deviceIndex);
        return;
      }
    } else if (subCmd == "fpga_config") {
      auto fpga_config_arg =
          static_cast<const CFGArg_PROGRAMMER_FPGA_CONFIG*>(arg->get_sub_arg());
      std::string bitstreamFile = fpga_config_arg->m_args[0];
      std::string cableInput = fpga_config_arg->cable;
      uint64_t deviceIndex = fpga_config_arg->index;
      if (!isHwDbInitialized) {
        InitializeHwDb(cableDeviceDb, cableMap, false);
        isHwDbInitialized = true;
      }
      auto cableIterator = cableMap.find(cableInput);
      if (cableIterator == cableMap.end()) {
        CFG_POST_ERR("Cable not found: %s", cableInput.c_str());
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      Cable cable = cableIterator->second;
      Device device;
      if (!findDeviceFromDb(cableDeviceDb, cable, deviceIndex, device)) {
        CFG_POST_ERR("Device not found: %d", deviceIndex);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      std::atomic<bool> stop = false;
      ProgressCallback progress = nullptr;
      auto gui = Gui::GuiInterface();
      if (gui) {
        progress = [gui](const std::string& progress) {
          gui->Progress(progress);
        };
        gui->ProgramFpga(cable, device, bitstreamFile);
      }
      status = ProgramFpga(
          cable, device, bitstreamFile, gui ? gui->Stop() : stop, nullptr,
          [](std::string msg) {
            std::string formatted;
            formatted = removeInfoAndNewline(msg);
            CFG_POST_MSG("%s", formatted.c_str());
          },
          progress);
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Status(cable, device, status);
      if (status != ProgrammerErrorCode::NoError) {
        CFG_POST_ERR("Failed to program FPGA. Error code: %d", status);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
    } else if (subCmd == "otp") {
      auto otp_arg =
          static_cast<const CFGArg_PROGRAMMER_OTP*>(arg->get_sub_arg());
      if (otp_arg->confirm == false) {
        CFG_post_msg(
            "WARNING: The OTP programming is not reversable. Please use -y to "
            "indicate your consensus to proceed.\n\n",
            "", false);
        return;
      }
      std::string bitstreamFile = otp_arg->m_args[0];
      std::string cableInput = otp_arg->cable;
      uint64_t deviceIndex = otp_arg->index;
      if (!isHwDbInitialized) {
        InitializeHwDb(cableDeviceDb, cableMap, false);
        isHwDbInitialized = true;
      }
      auto cableIterator = cableMap.find(cableInput);
      if (cableIterator == cableMap.end()) {
        CFG_POST_ERR("Cable not found: %s", cableInput.c_str());
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      Cable cable = cableIterator->second;
      Device device;
      if (!findDeviceFromDb(cableDeviceDb, cable, deviceIndex, device)) {
        CFG_POST_ERR("Device not found: %d", deviceIndex);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      ProgressCallback progress = nullptr;
      auto gui = Gui::GuiInterface();
      if (gui) {
        progress = [gui](const std::string& progress) {
          gui->Progress(progress);
        };
        gui->ProgramOtp(cable, device, bitstreamFile);
      }
      std::atomic<bool> stop = false;
      status = ProgramOTP(
          cable, device, bitstreamFile, gui ? gui->Stop() : stop, nullptr,
          [](std::string msg) {
            std::string formatted;
            formatted = removeInfoAndNewline(msg);
            CFG_POST_MSG("%s", formatted.c_str());
          },
          progress);
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Status(cable, device, status);
      if (status != ProgrammerErrorCode::NoError) {
        CFG_POST_ERR("Failed to program device OTP. Error code: %d", status);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
    } else if (subCmd == "flash") {
      auto flash_arg =
          static_cast<const CFGArg_PROGRAMMER_FLASH*>(arg->get_sub_arg());
      std::string bitstreamFile = flash_arg->m_args[0];
      std::string cableInput = flash_arg->cable;
      uint64_t deviceIndex = flash_arg->index;
      if (!isHwDbInitialized) {
        InitializeHwDb(cableDeviceDb, cableMap, false);
        isHwDbInitialized = true;
      }
      auto cableIterator = cableMap.find(cableInput);
      if (cableIterator == cableMap.end()) {
        CFG_POST_ERR("Cable not found: %s", cableInput.c_str());
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      Cable cable = cableIterator->second;
      Device device;
      if (!findDeviceFromDb(cableDeviceDb, cable, deviceIndex, device)) {
        CFG_POST_ERR("Device not found: %d", deviceIndex);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
      ProgressCallback progress = nullptr;
      auto gui = Gui::GuiInterface();
      if (gui) {
        progress = [gui](const std::string& progress) {
          gui->Progress(progress);
        };
        gui->Flash(cable, device, bitstreamFile);
      }
      std::atomic<bool> stop = false;
      status = ProgramFlash(
          cable, device, bitstreamFile, gui ? gui->Stop() : stop,
          ProgramFlashOperation::Program, nullptr,
          [](std::string msg) {
            std::string formatted;
            formatted = removeInfoAndNewline(msg);
            CFG_POST_MSG("%s", formatted.c_str());
          },
          progress);
      if (Gui::GuiInterface())
        Gui::GuiInterface()->Status(cable, device, status);
      if (status != ProgrammerErrorCode::NoError) {
        CFG_POST_ERR("Failed Flash programming. Error code: %d", status);
        cmdarg->tclStatus = TCL_ERROR;
        return;
      }
    }
  }
}

int InitLibrary(std::string openOCDPath) {
  if (openOCDPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }

  libOpenOcdExecPath = openOCDPath;

  if (!std::filesystem::exists(openOCDPath)) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }

  return ProgrammerErrorCode::NoError;
}

std::string GetErrorMessage(int errorCode) {
  auto it = ErrorMessages.find(errorCode);
  if (it != ErrorMessages.end()) {
    return it->second;
  }
  return "Unknown error.";
}

int GetAvailableCables(std::vector<Cable>& cables) {
#ifdef _MSC_VER
  return ProgrammerErrorCode::UnsupportedFunc;
#else
  struct libusb_context* jtagLibusbContext = nullptr; /**< Libusb context **/
  struct libusb_device** deviceList = nullptr; /**< The usb device list **/
  struct libusb_device_handle* libusbHandle = nullptr;
  std::string outputMsg;
  cables.clear();
  outputMsg.clear();
  int deviceCount = 0;
  int returnCode = 0;

  returnCode = libusb_init(&jtagLibusbContext);
  if (returnCode < 0) {
    outputMsg = "libusb_init() failed with " +
                std::string(libusb_error_name(returnCode)) + "\n";
    outputMsg += "GetAvailableCables() failed.\n";
    addOrUpdateErrorMessage(returnCode, outputMsg);
    return returnCode;
  }

  deviceCount = libusb_get_device_list(jtagLibusbContext, &deviceList);
  for (int index = 0; index < deviceCount; index++) {
    struct libusb_device_descriptor devDesc;

    if (libusb_get_device_descriptor(deviceList[index], &devDesc) != 0) {
      continue;
    }
    uint16_t cableIndex = 1;
    for (size_t i = 0; i < supportedCableVendorIdProductId.size(); i++) {
      if (devDesc.idVendor == std::get<0>(supportedCableVendorIdProductId[i]) &&
          devDesc.idProduct ==
              std::get<1>(supportedCableVendorIdProductId[i])) {
        Cable cable;
        cable.vendorId = devDesc.idVendor;
        cable.productId = devDesc.idProduct;
        cable.portAddr = libusb_get_port_number(deviceList[index]);
        cable.deviceAddr = libusb_get_device_address(deviceList[index]);
        cable.busAddr = libusb_get_bus_number(deviceList[index]);
        cable.name = "RsFtdi_" + std::to_string(cable.busAddr) + "_" +
                     std::to_string(cable.portAddr);
        cable.index = cableIndex++;
        returnCode = libusb_open(deviceList[index], &libusbHandle);

        if (returnCode) {
          outputMsg += "libusb_open() failed with " +
                       std::string(libusb_error_name(returnCode)) + "\n";
          outputMsg += "GetAvailableCables() failed.\n";
          addOrUpdateErrorMessage(returnCode, outputMsg);
          continue;
        }
        returnCode = get_string_descriptor(libusbHandle, devDesc.iProduct,
                                           cable.description, outputMsg);
        if (returnCode < 0) {
          addOrUpdateErrorMessage(returnCode, outputMsg);
          libusb_close(libusbHandle);
          continue;
        }

        if (get_string_descriptor(libusbHandle, devDesc.iSerialNumber,
                                  cable.serialNumber, outputMsg) < 0) {
          cable.serialNumber = "";  // ignore error, not all usb cable has
                                    // serial number
        }
        cables.push_back(cable);
        libusb_close(libusbHandle);
      }
    }
  }

  if (deviceList != nullptr) libusb_free_device_list(deviceList, 1);

  if (jtagLibusbContext != nullptr) libusb_exit(jtagLibusbContext);

  return ProgrammerErrorCode::NoError;
#endif  // _MSC_VER
}

int ListDevices(const Cable& cable, std::vector<Device>& devices) {
  if (libOpenOcdExecPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }
  int returnCode = ProgrammerErrorCode::NoError;
  std::string cmdOutput, outputMsg, listDeviceCmdOutput;
  std::atomic<bool> stopCommand{false};
  devices.clear();
  outputMsg.clear();
  foundTap.clear();

  returnCode = isCableSupported(cable);
  if (returnCode != ProgrammerErrorCode::NoError) {
    return returnCode;
  }

  std::string scanChainCmd = libOpenOcdExecPath + buildScanChainCommand(cable);
  returnCode = CFG_execute_cmd(scanChainCmd, cmdOutput, nullptr, stopCommand);
  if (returnCode) {
    outputMsg = "Failed to execute following command " + scanChainCmd +
                ". Error code: " + std::to_string(returnCode) + "\n";
    outputMsg += "ListDevices() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            outputMsg);
    returnCode = ProgrammerErrorCode::FailedExecuteCommand;
    return ProgrammerErrorCode::FailedExecuteCommand;
  }
  auto availableTap = extractTapInfoList(cmdOutput);
  for (auto& tap : availableTap) {
    for (auto& supportedTap : supportedTAP) {
      if (tap.idCode == supportedTap.idCode) {
        foundTap.push_back(TapInfo{tap.index, tap.tapName, tap.enabled,
                                   tap.idCode, tap.expected, tap.irLen,
                                   tap.irCap, tap.irMask});
      }
    }
  }

  if (foundTap.size() == 0) {
    outputMsg = GetErrorMessage(ProgrammerErrorCode::NoSupportedTapFound);
    return ProgrammerErrorCode::NoSupportedTapFound;
  }

  std::string listDeviceCmd =
      libOpenOcdExecPath + buildListDeviceCommand(cable, foundTap);
  returnCode =
      CFG_execute_cmd(listDeviceCmd, listDeviceCmdOutput, nullptr, stopCommand);
  if (returnCode) {
    outputMsg += "Failed to execute following command " + listDeviceCmd +
                 ". Error code: " + std::to_string(returnCode) + "\n";
    outputMsg += "ListDevices() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            outputMsg);
    returnCode = ProgrammerErrorCode::FailedExecuteCommand;
    return ProgrammerErrorCode::FailedExecuteCommand;
  }
  returnCode = extractDeviceList(listDeviceCmdOutput, devices);
  if (returnCode) {
    outputMsg += "Failed to extract device list from command output:\n" +
                 listDeviceCmdOutput + "\n";
    outputMsg += "ListDevices() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::InvalidFlashSize, outputMsg);
    return ProgrammerErrorCode::InvalidFlashSize;
  }
  return ProgrammerErrorCode::NoError;
}

int GetFpgaStatus(const Cable& cable, const Device& device, CfgStatus& status,
                  std::string& statusOutputPrint) {
  if (libOpenOcdExecPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }
  int returnCode = ProgrammerErrorCode::NoError;
  std::string cmdOutput, outputMsg;
  bool found = false;
  std::atomic<bool> stopCommand{false};

  returnCode = isCableSupported(cable);
  if (returnCode != ProgrammerErrorCode::NoError) {
    return returnCode;
  }

  std::string queryFpgaStatusCmd =
      libOpenOcdExecPath + buildFpgaQueryStatusCommand(cable, device);
  returnCode =
      CFG_execute_cmd(queryFpgaStatusCmd, cmdOutput, nullptr, stopCommand);
  if (returnCode) {
    outputMsg = "Failed to execute following command " + queryFpgaStatusCmd +
                ". Error code: " + std::to_string(returnCode) + "\n";
    outputMsg += "GetFpgaStatus() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            outputMsg);
    return ProgrammerErrorCode::FailedExecuteCommand;
  }
  status = extractStatus(cmdOutput, found);
  if (!found) {
    outputMsg =
        "Failed to extract status from command output:\n" + cmdOutput + "\n";
    outputMsg += "GetFpgaStatus() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedToParseOutput,
                            outputMsg);
    returnCode = ProgrammerErrorCode::FailedToParseOutput;
  }
  statusOutputPrint = cmdOutput;
  outputMsg = "FPGA configuration status found.\n";
  return returnCode;
}

int ProgramFpga(const Cable& cable, const Device& device,
                const std::string& bitfile, std::atomic<bool>& stop,
                std::ostream* outStream /*=nullptr*/,
                OutputMessageCallback callbackMsg /*=nullptr*/,
                ProgressCallback callbackProgress /*=nullptr*/) {
  if (libOpenOcdExecPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }
  int returnCode = ProgrammerErrorCode::NoError;
  std::error_code ec;
  std::string errorMessage;
  if (!std::filesystem::exists(bitfile, ec)) {
    errorMessage = "Cannot find bitfile: " + bitfile + ". " + ec.message();
    returnCode = ProgrammerErrorCode::BitfileNotFound;
    addOrUpdateErrorMessage(returnCode, errorMessage);
    return returnCode;
  }
  std::string cmdOutput;
  std::string programFpgaCommand =
      libOpenOcdExecPath + buildFpgaProgramCommand(cable, device, bitfile);
  std::string progressPercentagePattern = "\\d{1,3}\\.\\d{2}";
  std::regex regexPattern(progressPercentagePattern);
  returnCode = CFG_execute_cmd_with_callback(programFpgaCommand, cmdOutput,
                                             outStream, regexPattern, stop,
                                             callbackProgress, callbackMsg);
  if (returnCode) {
    errorMessage = "Failed to execute following command " + programFpgaCommand +
                   ". Error code: " + std::to_string(returnCode) + "\n";
    errorMessage += "ProgramFpga() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            errorMessage);
    return ProgrammerErrorCode::FailedExecuteCommand;
  }

  // when programming done, openocd will print out "loaded file"
  // loaded file /home/user1/abc.bin to device 0 in 5s 90381us
  size_t pos = cmdOutput.find("loaded file");
  if (pos == std::string::npos) {
    returnCode = ProgrammerErrorCode::FailedToProgramFPGA;
  }
  return returnCode;
}

int ProgramOTP(const Cable& cable, const Device& device,
               const std::string& bitfile, std::atomic<bool>& stop,
               std::ostream* outStream /*=nullptr*/,
               OutputMessageCallback callbackMsg /*=nullptr*/,
               ProgressCallback callbackProgress /*=nullptr*/) {
  if (libOpenOcdExecPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }
  int returnCode = ProgrammerErrorCode::NoError;
  std::error_code ec;
  std::string errorMessage;
  if (!std::filesystem::exists(bitfile, ec)) {
    errorMessage = "Cannot find bitfile: " + bitfile + ". " + ec.message();
    returnCode = ProgrammerErrorCode::BitfileNotFound;
    addOrUpdateErrorMessage(returnCode, errorMessage);
    return returnCode;
  }
  std::string cmdOutput;
  std::string programOTPCommand =
      libOpenOcdExecPath + buildOTPProgramCommand(cable, device, bitfile);
  std::string progressPercentagePattern = "\\d{1,3}\\.\\d{2}";
  std::regex regexPattern(progressPercentagePattern);
  returnCode = CFG_execute_cmd_with_callback(programOTPCommand, cmdOutput,
                                             outStream, regexPattern, stop,
                                             callbackProgress, callbackMsg);
  if (returnCode) {
    errorMessage = "Failed to execute following command " + programOTPCommand +
                   ". Error code: " + std::to_string(returnCode) + "\n";
    errorMessage += "ProgramOTP() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            errorMessage);
    return ProgrammerErrorCode::FailedExecuteCommand;
  }

  // when programming done, openocd will print out "loaded file"
  // loaded file /home/user1/abc.bin to device 0 in 5s 90381us
  size_t pos = cmdOutput.find("loaded file");
  if (pos == std::string::npos) {
    returnCode = ProgrammerErrorCode::FailedToProgramOTP;
  }
  return returnCode;
}

int ProgramFlash(
    const Cable& cable, const Device& device, const std::string& bitfile,
    std::atomic<bool>& stop,
    ProgramFlashOperation modes /*= (ProgramFlashOperation::Erase |
                                   ProgramFlashOperation::Program)*/
    ,
    std::ostream* outStream /*=nullptr*/,
    OutputMessageCallback callbackMsg /*=nullptr*/,
    ProgressCallback callbackProgress /*=nullptr*/) {
  if (libOpenOcdExecPath.empty()) {
    return ProgrammerErrorCode::OpenOCDExecutableNotFound;
  }
  int returnCode = ProgrammerErrorCode::NoError;
  std::error_code ec;
  std::string errorMessage;
  if (!std::filesystem::exists(bitfile, ec)) {
    errorMessage = "Cannot find bitfile: " + bitfile + ". " + ec.message();
    returnCode = ProgrammerErrorCode::BitfileNotFound;
    addOrUpdateErrorMessage(returnCode, errorMessage);
    return returnCode;
  }
  std::string cmdOutput;
  std::string programFlashCommand =
      libOpenOcdExecPath +
      buildFlashProgramCommand(cable, device, bitfile, modes);
  std::string progressPercentagePattern = "\\d{1,3}\\.\\d{2}";
  std::regex regexPattern(progressPercentagePattern);
  returnCode = CFG_execute_cmd_with_callback(programFlashCommand, cmdOutput,
                                             outStream, regexPattern, stop,
                                             callbackProgress, callbackMsg);
  if (returnCode) {
    errorMessage = "Failed to execute following command " +
                   programFlashCommand +
                   ". Error code: " + std::to_string(returnCode) + "\n";
    errorMessage += "ProgramFlash() failed.\n";
    addOrUpdateErrorMessage(ProgrammerErrorCode::FailedExecuteCommand,
                            errorMessage);
    return ProgrammerErrorCode::FailedExecuteCommand;
  }

  // when programming done, openocd will print out "loaded file"
  // loaded file /home/user1/abc.bin to device 0 in 5s 90381us
  size_t pos = cmdOutput.find("loaded file");
  if (pos == std::string::npos) {
    returnCode = ProgrammerErrorCode::FailedToProgramFPGA;
  }
  return 0;
}

}  // namespace FOEDAG
