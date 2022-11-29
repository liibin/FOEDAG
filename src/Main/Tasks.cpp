/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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

#include "Tasks.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "Compiler/Reports/ITaskReport.h"
#include "Compiler/Reports/ITaskReportManager.h"
#include "Foedag.h"
#include "TextEditor/text_editor_form.h"
#include "WidgetFactory.h"

using namespace FOEDAG;

#define TASKS_KEY "Tasks"
#define SYNTH_ARG "_SynthOpt_"
#define PLACE_ARG "pin_assign_method"

#define TASKS_DEBUG false

namespace {
void openReportView(const ITaskReport& report) {
  auto reportsView = new QTableWidget();

  // Fill columns
  auto columns = report.getColumns();
  reportsView->setColumnCount(columns.size());
  auto colIndex = 0;
  for (auto& col : columns) {
    auto columnItem = new QTableWidgetItem(col);
    reportsView->setHorizontalHeaderItem(colIndex, columnItem);
    ++colIndex;
  }

  // Fill table
  auto rowIndex = 0;
  for (auto& lineData : report.getData()) {
    reportsView->insertRow(rowIndex);
    auto colIndex = 0;
    for (auto& lineValue : lineData) {
      auto item = new QTableWidgetItem(lineValue);
      item->setTextAlignment(colIndex == 0 ? Qt::AlignLeft | Qt::AlignVCenter
                                           : Qt::AlignCenter);
      reportsView->setItem(rowIndex, colIndex, item);
      ++colIndex;
    }
    ++rowIndex;
  }

  // Initialize the view itself
  reportsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  reportsView->horizontalHeader()->resizeSections(
      QHeaderView::ResizeToContents);

  auto tabWidget = TextEditorForm::Instance()->GetTabWidget();
  tabWidget->addTab(reportsView, report.getName());
  tabWidget->setCurrentWidget(reportsView);
}
}  // namespace

auto TASKS_DBG_PRINT = [](std::string printStr) {
  if (TASKS_DEBUG) {
    std::cout << printStr << std::flush;
  }
};

// Grab a specific arg and value from a list of args and return that specific
// pair as well as the rest of the args w/ that specifc arg removed
auto separateArg = [](const QString& argName,
                      const QString& argString) -> std::pair<QString, QString> {
  QString targetArg = "";
  QString otherArgs = argString;
  QString searchStr = argName;

  if (!searchStr.isEmpty()) {
    // prepend - if one doesn't exist
    if (searchStr[0] != "-") {
      searchStr = "-" + searchStr;
    }
    // Find the arg and remove it from the otherArgs
    auto argIdx = argString.indexOf("-" + QString(SYNTH_ARG));
    if (argIdx != -1) {
      targetArg = argString.mid(argIdx, argString.indexOf("-", argIdx + 1));
      otherArgs = otherArgs.replace(targetArg, "");
    }
    auto argPlaceIdx = argString.indexOf("-" + QString(PLACE_ARG));
    if (argPlaceIdx != -1) {
      targetArg =
          argString.mid(argPlaceIdx, argString.indexOf("-", argPlaceIdx + 1));
      otherArgs = otherArgs.replace(targetArg, "");
    }
  }
  return {targetArg, otherArgs};
};

// Lookup for SynthOpt values
static std::map<FOEDAG::Compiler::SynthesisOpt, const char*> synthOptMap = {
    {FOEDAG::Compiler::SynthesisOpt::None, "none"},
    {FOEDAG::Compiler::SynthesisOpt::Area, "area"},
    {FOEDAG::Compiler::SynthesisOpt::Delay, "delay"},
    {FOEDAG::Compiler::SynthesisOpt::Mixed, "mixed"},
    {FOEDAG::Compiler::SynthesisOpt::Clean, "clean"}};
// Lookup for PlaceOpt values
static std::map<FOEDAG::Compiler::PinAssignOpt, const char*> pinOptMap = {
    {FOEDAG::Compiler::PinAssignOpt::Random, "random"},
    {FOEDAG::Compiler::PinAssignOpt::In_Define_Order, "in_define_order"},
    {FOEDAG::Compiler::PinAssignOpt::Free, "free"}};

// Helper to convert a SynthesisOpt enum to string
auto synthOptToStr = [](FOEDAG::Compiler::SynthesisOpt opt) -> QString {
  return synthOptMap[opt];
};

// Helper to convert a string to SynthesisOpt enum
auto synthStrToOpt = [](const QString& str) -> FOEDAG::Compiler::SynthesisOpt {
  auto it = find_if(
      synthOptMap.begin(), synthOptMap.end(),
      [str](const std::pair<FOEDAG::Compiler::SynthesisOpt, const char*> p) {
        return p.second == str;
      });

  auto val = FOEDAG::Compiler::SynthesisOpt::None;
  if (it != synthOptMap.end()) {
    val = (*it).first;
  }

  return val;
};

auto pinOptToStr = [](FOEDAG::Compiler::PinAssignOpt opt) -> QString {
  return pinOptMap.at(opt);
};

auto pinStrToOpt = [](const QString& str) -> FOEDAG::Compiler::PinAssignOpt {
  auto it = find_if(
      pinOptMap.begin(), pinOptMap.end(),
      [str](const std::pair<FOEDAG::Compiler::PinAssignOpt, const char*> p) {
        return p.second == str;
      });

  auto val = FOEDAG::Compiler::PinAssignOpt::In_Define_Order;
  if (it != pinOptMap.end()) {
    val = (*it).first;
  }

  return val;
};
// This will grab Placement related options from Compiler::PlaceOpt &
// Compiler::PlaceMoreOpt, convert/combine them, and return them as an
// arg list QString
std::string FOEDAG::TclArgs_getSynthesisOptions() {
  // Collect Synthesis Tcl Params
  QString tclOptions =
      QString::fromStdString(GlobalSession->GetCompiler()->SynthMoreOpt());
  // Syntehsis has one top level option that doesn't get passed with
  // SynthMoreOpt so we need to give it a fake arg and pass it
  tclOptions += " -" + QString(SYNTH_ARG) + " " +
                synthOptToStr(GlobalSession->GetCompiler()->SynthOpt());
  return tclOptions.toStdString();
};

// This will take an arg list, separate out the SynthOpt to set on the compiler
// and then set the rest of the options under SynthMoreOpt
void FOEDAG::TclArgs_setSynthesisOptions(const std::string& argsStr) {
  auto [synthArg, moreOpts] =
      separateArg(SYNTH_ARG, QString::fromStdString(argsStr).trimmed());
  FOEDAG::Compiler* compiler = GlobalSession->GetCompiler();
  if (compiler) {
    QStringList tokens = synthArg.split(" ");
    if (tokens.count() > 1) {
      compiler->SynthOpt(synthStrToOpt(tokens[1]));
    }
    compiler->SynthMoreOpt(moreOpts.toStdString());
  }
};

std::string FOEDAG::TclArgs_getPlacementOptions() {
  // Collect placement Tcl Params
  QString tclOptions =
      QString::fromStdString(GlobalSession->GetCompiler()->PlaceMoreOpt());
  tclOptions += " -" + QString(PLACE_ARG) + " " +
                pinOptToStr(GlobalSession->GetCompiler()->PinAssignOpts());
  return tclOptions.toStdString();
}

void FOEDAG::TclArgs_setPlacementOptions(const std::string& argsStr) {
  auto [pinArg, moreOpts] =
      separateArg(PLACE_ARG, QString::fromStdString(argsStr));
  FOEDAG::Compiler* compiler = GlobalSession->GetCompiler();
  if (compiler) {
    QStringList tokens = pinArg.split(" ");
    if (tokens.count() > 1) {
      compiler->PinAssignOpts(pinStrToOpt(tokens[1]));
    }
    compiler->PlaceMoreOpt(moreOpts.toStdString());
  }
}

// Hardcoded example callbacks to demonstrate how to use TclArgs with the task
// settings dialog
// NOTE: Do not do UI/integration (unit is ok) testing with this example as its
// initial hardcoding can make some settings aspects like loading saved values
// seem broken
static QString TclExampleArgs =
    "-double_spin_ex 3.3 -int_spin_ex 3 -radio_ex b3 -check_ex -dropdown_ex "
    "option3 -input_ex "
    "spaces_TclArgSpace_require_TclArgSpace_extra_TclArgSpace_formatting";

std::string FOEDAG::TclArgs_getExampleArgs() {
  return TclExampleArgs.toStdString();
};
void FOEDAG::TclArgs_setExampleArgs(const std::string& argsStr) {
  TclExampleArgs = QString::fromStdString(argsStr);
};

QDialog* FOEDAG::createTaskDialog(const QString& taskName) {
  QString title = "Edit " + taskName + " Settings";
  QString prefix = "tasksDlg_" + taskName + "_";

  return FOEDAG::createSettingsDialog("/Tasks/" + taskName, title, prefix);
};

void FOEDAG::handleTaskDialogRequested(const QString& category) {
  QDialog* dlg = createTaskDialog(category);
  if (dlg) {
    dlg->exec();
  }
}

void FOEDAG::handleViewFileRequested(const QString& filePath) {
  QString path = filePath;
  path.replace(PROJECT_OSRCDIR, Project::Instance()->projectPath());
  TextEditorForm::Instance()->OpenFile(path);
}

void FOEDAG::handleViewReportRequested(ITaskReportManager& reportManager) {
  auto reports = std::vector<std::unique_ptr<ITaskReport>>{};
  auto availableReports = reportManager.getAvailableReportIds();
  if (availableReports.empty()) return;

  reports.reserve(availableReports.size());
  for (auto& reportId : availableReports)
    reports.push_back(reportManager.createReport(reportId));

  for (auto& report : reports) {
    if (report) openReportView(*report);
  }
}
