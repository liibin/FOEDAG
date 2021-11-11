#ifndef SUMMARYFORM_H
#define SUMMARYFORM_H

#include <QWidget>

namespace Ui {
class summaryForm;
}

class summaryForm : public QWidget {
  Q_OBJECT

 public:
  explicit summaryForm(QWidget *parent = nullptr);
  ~summaryForm();

  void setProjectName(const QString &proName, const int &proType);
  void setSourceCount(const int &srcCount, const int constrCount);
  void setDeviceInfo(const QList<QString> listDevItem);

 private:
  Ui::summaryForm *ui;
};

#endif  // SUMMARYFORM_H
