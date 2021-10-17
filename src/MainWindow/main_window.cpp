#include <fstream>
#include <QTextStream>
#include <QtWidgets>

#include "main_window.h"

MainWindow::MainWindow() {
  /* Create actions that can be added to menu/tool bars */
  createActions();

  /* Create menu bars */
  createMenus();

  /* Create tool bars */
  createToolBars();

  /* Create status bar */
  statusBar();

  setWindowTitle(tr("FOEDAG"));
}

void MainWindow::newFile() {
  QTextStream out(stdout);
  out << "New file is requested" << endl;
}

void MainWindow::createMenus() {
  fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(newAction);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAction);
}

void MainWindow::createToolBars() {
  fileToolBar = addToolBar(tr("&File"));
  fileToolBar->addAction(newAction);
}

void MainWindow::createActions() {
  newAction = new QAction(tr("&New"), this);
  newAction->setIcon(QIcon(":/images/icon_newfile.png"));
  newAction->setShortcut(QKeySequence::New); 
  newAction->setStatusTip(tr("Create a new source file"));
  connect(newAction, SIGNAL(triggered()), this, SLOT(newFile()));

  exitAction = new QAction(tr("E&xit"), this);
  exitAction->setShortcut(tr("Ctrl+Q"));
  exitAction->setStatusTip(tr("Exit the application"));
  connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);
}
