#ifndef NEWFILE_H
#define NEWFILE_H

#include <QFileDialog>
#include <QObject>
#include <QWidget>

#define FILTER_VERILOG "Verilog HDL Files(*.v)"
#define FILTER_VHDL "VHDL Files(*.vhd)"
#define FILTER_TCL "Tcl Script Files(*.tcl)"
#define FILTER_CONSTR "Synopsys Design Constraints Files(*.sdc)"
#define FILTER_ALL "All Files(*.*)"

namespace FOEDAG {

class NewFile : public QWidget {
  Q_OBJECT
 public:
  explicit NewFile(QWidget* parent = nullptr);

  void StartNewFile();
  void StopNewFile();
 signals:

 private:
  QFileDialog* m_fileDialog;
};
}  // namespace FOEDAG
#endif  // NEWFILE_H
