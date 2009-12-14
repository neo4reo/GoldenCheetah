/* 
 * Copyright (c) 2006 Sean C. Rhea (srhea@srhea.net)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "MainWindow.h"
#include "AllPlotWindow.h"
#include "AllPlot.h"
#include "BestIntervalDialog.h"
#include "ChooseCyclistDialog.h"
#include "Computrainer.h"
#include "ConfigDialog.h"
#include "CriticalPowerWindow.h"
#include "GcRideFile.h"
#include "PfPvWindow.h"
#include "DownloadRideDialog.h"
#include "ManualRideDialog.h"
#include "HistogramWindow.h"
#include "RealtimeWindow.h"
#include "RideItem.h"
#include "IntervalItem.h"
#include "RideFile.h"
#include "RideSummaryWindow.h"
#include "RideImportWizard.h"
#include "QuarqRideFile.h"
#include "RideMetric.h"
#include "Settings.h"
#include "TimeUtils.h"
#include "Units.h"
#include "WeeklySummaryWindow.h"
#include "Zones.h"
#include <assert.h>
#include <QApplication>
#include <QtGui>
#include <QRegExp>
#include <qwt_plot_curve.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_grid.h>
#include <qwt_data.h>
#include <boost/scoped_ptr.hpp>
#include "RideCalendar.h"
#include "DatePickerDialog.h"
#include "ToolsDialog.h"
#include "MetricAggregator.h"
#include "SplitRideDialog.h"
#include "PerformanceManagerWindow.h"

#ifndef GC_VERSION
#define GC_VERSION "(developer build)"
#endif

#define FOLDER_TYPE 0
#define RIDE_TYPE 1

bool
MainWindow::parseRideFileName(const QString &name, QString *notesFileName, QDateTime *dt)
{
    static char rideFileRegExp[] = "^((\\d\\d\\d\\d)_(\\d\\d)_(\\d\\d)"
                                   "_(\\d\\d)_(\\d\\d)_(\\d\\d))\\.(.+)$";
    QRegExp rx(rideFileRegExp);
    if (!rx.exactMatch(name))
        return false;
    assert(rx.numCaptures() == 8);
    QDate date(rx.cap(2).toInt(), rx.cap(3).toInt(),rx.cap(4).toInt()); 
    QTime time(rx.cap(5).toInt(), rx.cap(6).toInt(),rx.cap(7).toInt()); 
    if ((! date.isValid()) || (! time.isValid())) {
	QMessageBox::warning(this,
			     tr("Invalid Ride File Name"),
			     tr("Invalid date/time in filename:\n%1\nSkipping file...").arg(name)
			     );
	return false;
    }
    *dt = QDateTime(date, time);
    *notesFileName = rx.cap(1) + ".notes";
    return true;
}

MainWindow::MainWindow(const QDir &home) : 
    home(home), 
    zones_(new Zones), currentNotesChanged(false),
    ride(NULL)
{
    setAttribute(Qt::WA_DeleteOnClose);

    settings = GetApplicationSettings();
      
    QVariant unit = settings->value(GC_UNIT);
    useMetricUnits = (unit.toString() == "Metric");

    setWindowTitle(home.dirName());
    settings->setValue(GC_SETTINGS_LAST, home.dirName());

    setWindowIcon(QIcon(":images/gc.png"));
    setAcceptDrops(true);

    QFile zonesFile(home.absolutePath() + "/power.zones");
    if (zonesFile.exists()) {
        if (!zones_->read(zonesFile)) {
            QMessageBox::critical(this, tr("Zones File Error"),
				  zones_->errorString());
            zones_->clear();
        }
	else if (! zones_->warningString().isEmpty())
            QMessageBox::warning(this, tr("Reading Zones File"), zones_->warningString());
    }

    QVariant geom = settings->value(GC_SETTINGS_MAIN_GEOM);
    if (geom == QVariant())
        resize(640, 480);
    else
        setGeometry(geom.toRect());

    splitter = new QSplitter(this);
    setCentralWidget(splitter);
    splitter->setContentsMargins(10, 20, 10, 10); // attempting to follow some UI guides

    calendar = new RideCalendar;
    calendar->setFirstDayOfWeek(Qt::Monday);
    calendar->setHome(home);
    calendar->addWorkoutCode(QString("race"), QColor(Qt::red));
    calendar->addWorkoutCode(QString("sick"), QColor(Qt::yellow));
    calendar->addWorkoutCode(QString("swim"), QColor(Qt::blue));
    calendar->addWorkoutCode(QString("gym"), QColor(Qt::gray));

    treeWidget = new QTreeWidget;
    treeWidget->setColumnCount(3);
    treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    // TODO: Test this on various systems with differing font settings (looks good on Leopard :)
    treeWidget->header()->resizeSection(0,70);
    treeWidget->header()->resizeSection(1,95);
    treeWidget->header()->resizeSection(2,70);
    //treeWidget->setMaximumWidth(250);
    treeWidget->header()->hide();
    treeWidget->setAlternatingRowColors (true);
    treeWidget->setIndentation(5);

    allRides = new QTreeWidgetItem(treeWidget, FOLDER_TYPE);
    allRides->setText(0, tr("All Rides"));
    treeWidget->expandItem(allRides);

    intervalWidget = new QTreeWidget(this);
    intervalWidget->setColumnCount(1);
    intervalWidget->setIndentation(5);
    intervalWidget->setSortingEnabled(false);
    intervalWidget->header()->hide();
    intervalWidget->setAlternatingRowColors (true);
    intervalWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    intervalWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    intervalWidget->setSelectionMode(QAbstractItemView::MultiSelection);
    intervalWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    allIntervals = new QTreeWidgetItem(intervalWidget, FOLDER_TYPE);
    allIntervals->setText(0, tr("Intervals"));
    intervalWidget->expandItem(allIntervals);

    intervalsplitter = new QSplitter(this);
    intervalsplitter->setOrientation(Qt::Vertical);
    intervalsplitter->addWidget(treeWidget);
    intervalsplitter->setCollapsible(0, true);
    intervalsplitter->addWidget(intervalWidget);
    intervalsplitter->setCollapsible(1, true);

    leftLayout = new QSplitter;
    leftLayout->setOrientation(Qt::Vertical);
    leftLayout->addWidget(calendar);
    leftLayout->setCollapsible(0, true);
    leftLayout->addWidget(intervalsplitter);
    leftLayout->setCollapsible(1, false);
    splitter->addWidget(leftLayout);
    splitter->setCollapsible(0, true);
    QVariant calendarSizes = settings->value(GC_SETTINGS_CALENDAR_SIZES);
    if (calendarSizes != QVariant()) {
        leftLayout->restoreState(calendarSizes.toByteArray());
    }

    QTreeWidgetItem *last = NULL;
    QStringListIterator i(RideFileFactory::instance().listRideFiles(home));
    while (i.hasNext()) {
        QString name = i.next(), notesFileName;
        QDateTime dt;
        if (parseRideFileName(name, &notesFileName, &dt)) {
            last = new RideItem(RIDE_TYPE, home.path(), 
                                name, dt, zones(), notesFileName);
            allRides->addChild(last);
	    calendar->addRide(reinterpret_cast<RideItem*>(last));
        }
    }

    tabWidget = new QTabWidget;
    tabWidget->setUsesScrollButtons(true);

    rideSummaryWindow = new RideSummaryWindow(this);
    QLabel *notesLabel = new QLabel(tr("Notes:"));
    notesLabel->setMaximumHeight(30);
    rideNotes = new QTextEdit;

    notesWidget = new QWidget();
    notesLayout = new QVBoxLayout(notesWidget);
    notesLayout->addWidget(notesLabel);
    notesLayout->addWidget(rideNotes);

    summarySplitter = new QSplitter;
    summarySplitter->setContentsMargins(0, 0, 0, 0);
    summarySplitter->setOrientation(Qt::Vertical);
    summarySplitter->addWidget(rideSummaryWindow);
    summarySplitter->setCollapsible(0, false);
    summarySplitter->addWidget(notesWidget);
    summarySplitter->setCollapsible(1, true);

    // the sizes are somewhat arbitrary,
    // just trying to force the smallest non-hidden notes size by default
    QList<int> summarySizes;
    summarySizes.append(800);
    summarySizes.append(200);
    summarySplitter->setSizes(summarySizes);

    tabWidget->addTab(summarySplitter, tr("Ride Summary"));

    /////////////////////////// Ride Plot Tab ///////////////////////////
    allPlotWindow = new AllPlotWindow(this);
    tabWidget->addTab(allPlotWindow, tr("Ride Plot"));
    splitter->addWidget(tabWidget);
    splitter->setCollapsible(1, true);

    QVariant splitterSizes = settings->value(GC_SETTINGS_SPLITTER_SIZES); 
    if (splitterSizes != QVariant())
        splitter->restoreState(splitterSizes.toByteArray());
    else {
        QList<int> sizes;
        sizes.append(250);
        sizes.append(390);
        splitter->setSizes(sizes);
    }

    ////////////////////// Critical Power Plot Tab //////////////////////

    criticalPowerWindow = new CriticalPowerWindow(home, this);
    tabWidget->addTab(criticalPowerWindow, tr("Critical Power Plot"));

    //////////////////////// Power Histogram Tab ////////////////////////

    histogramWindow = new HistogramWindow(this);
    tabWidget->addTab(histogramWindow, "Histogram Analysis");
    
    //////////////////////// Pedal Force/Velocity Plot ////////////////////////

    pfPvWindow = new PfPvWindow(this);
    tabWidget->addTab(pfPvWindow, tr("PF/PV Plot"));

    //////////////////////// Weekly Summary ////////////////////////
    
    // add daily distance / duration graph:
    weeklySummaryWindow = new WeeklySummaryWindow(useMetricUnits, this);
    tabWidget->addTab(weeklySummaryWindow, tr("Weekly Summary"));

    //////////////////////// Performance Manager  ////////////////////////

    performanceManagerWindow = new PerformanceManagerWindow(this);
    tabWidget->addTab(performanceManagerWindow, "Performance Manager");

    //////////////////////// Realtime ////////////////////////

    realtimeWindow = new RealtimeWindow(this, home);
    tabWidget->addTab(realtimeWindow, tr("Realtime"));

    ////////////////////////////// Signals ////////////////////////////// 

    connect(calendar, SIGNAL(clicked(const QDate &)),
            this, SLOT(dateChanged(const QDate &)));
    connect(leftLayout, SIGNAL(splitterMoved(int,int)),
            this, SLOT(leftLayoutMoved()));
    connect(treeWidget, SIGNAL(itemSelectionChanged()),
            this, SLOT(rideTreeWidgetSelectionChanged()));
    connect(splitter, SIGNAL(splitterMoved(int,int)), 
            this, SLOT(splitterMoved()));
    connect(tabWidget, SIGNAL(currentChanged(int)), 
            this, SLOT(tabChanged(int)));
    connect(rideNotes, SIGNAL(textChanged()),
            this, SLOT(notesChanged()));
    connect(intervalWidget,SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(showContextMenuPopup(const QPoint &)));
    connect(intervalWidget,SIGNAL(itemSelectionChanged()),
            this, SLOT(intervalTreeWidgetSelectionChanged()));
    connect(intervalWidget,SIGNAL(itemChanged(QTreeWidgetItem *,int)),
            this, SLOT(intervalEdited(QTreeWidgetItem*, int)));


    /////////////////////////////// Menus ///////////////////////////////

    QMenu *fileMenu = menuBar()->addMenu(tr("&Cyclist"));
    fileMenu->addAction(tr("&New..."), this, 
                        SLOT(newCyclist()), tr("Ctrl+N")); 
    fileMenu->addAction(tr("&Open..."), this, 
                        SLOT(openCyclist()), tr("Ctrl+O")); 
    fileMenu->addAction(tr("&Quit"), this, 
                        SLOT(close()), tr("Ctrl+Q")); 

    QMenu *rideMenu = menuBar()->addMenu(tr("&Ride"));
    rideMenu->addAction(tr("&Save Ride"), this,
                        SLOT(saveRide()), tr("Ctrl+S"));
    rideMenu->addAction(tr("&Download from device..."), this, 
                        SLOT(downloadRide()), tr("Ctrl+D")); 
    rideMenu->addAction(tr("&Export to CSV..."), this, 
                        SLOT(exportCSV()), tr("Ctrl+E")); 
    rideMenu->addAction(tr("&Export to GC..."), this,
                        SLOT(exportGC()));
    rideMenu->addAction(tr("&Import from File..."), this,
                        SLOT (importFile()), tr ("Ctrl+I"));
    rideMenu->addAction(tr("Find &best intervals..."), this,
                        SLOT(findBestIntervals()), tr ("Ctrl+B"));
    rideMenu->addAction(tr("Find power &peaks..."), this,
                        SLOT(findPowerPeaks()), tr ("Ctrl+P"));
    rideMenu->addAction(tr("Split &ride..."), this,
                        SLOT(splitRide()));
    rideMenu->addAction(tr("D&elete ride..."), this,
                        SLOT(deleteRide()));
    rideMenu->addAction(tr("&Manual ride entry..."), this,
          SLOT(manualRide()), tr("Ctrl+M"));

    QMenu *optionsMenu = menuBar()->addMenu(tr("&Tools"));
    optionsMenu->addAction(tr("&Options..."), this, 
                           SLOT(showOptions()), tr("Ctrl+O")); 
    optionsMenu->addAction(tr("Critical Power Calculator"), this,
                           SLOT(showTools()));
    //optionsMenu->addAction(tr("&Reset Metrics..."), this, 
    //                       SLOT(importRideToDB()), tr("Ctrl+R")); 
    //optionsMenu->addAction(tr("&Update Metrics..."), this, 
    //                       SLOT(scanForMissing()()), tr("Ctrl+U")); 

 
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About GoldenCheetah"), this, SLOT(aboutDialog()));

       
    QVariant isAscending = settings->value(GC_ALLRIDES_ASCENDING,Qt::Checked);
    if(isAscending.toInt()>0){
            if (last != NULL)
                treeWidget->setCurrentItem(last);
    } else {
        // selects the first ride in the list:
        if (allRides->child(0) != NULL){
            treeWidget->scrollToItem(allRides->child(0), QAbstractItemView::EnsureVisible);
            treeWidget->setCurrentItem(allRides->child(0));
        }
    }

    setAttribute(Qt::WA_DeleteOnClose);
}

void
MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction(); // whatever you wanna drop we will try and process!
}

void
MainWindow::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    // We have something to process then
    RideImportWizard *dialog = new RideImportWizard (&urls, home, this);
    dialog->process(); // do it!
    return;
}

void
MainWindow::addRide(QString name, bool bSelect /*=true*/)
{
    QString notesFileName;
    QDateTime dt;
    if (!parseRideFileName(name, &notesFileName, &dt)) {
        fprintf(stderr, "bad name: %s\n", name.toAscii().constData());
        assert(false);
    }
    RideItem *last = new RideItem(RIDE_TYPE, home.path(), 
                                  name, dt, zones(), notesFileName);

    QVariant isAscending = settings->value(GC_ALLRIDES_ASCENDING,Qt::Checked); // default is ascending sort
    int index = 0;
    while (index < allRides->childCount()) {
        QTreeWidgetItem *item = allRides->child(index);
        if (item->type() != RIDE_TYPE)
            continue;
        RideItem *other = reinterpret_cast<RideItem*>(item);
        
        if(isAscending.toInt() > 0 ){
            if (other->dateTime > dt)
                break;
        } else {
            if (other->dateTime < dt)
                break; 
        }
        if (other->fileName == name) {
            delete allRides->takeChild(index);
            break;
        }
        ++index;
    }
    allRides->insertChild(index, last);
    calendar->addRide(last);
    criticalPowerWindow->newRideAdded();
    if (bSelect)
    {
        tabWidget->setCurrentIndex(0);
        treeWidget->setCurrentItem(last);
    }
}

void
MainWindow::removeCurrentRide()
{
    QTreeWidgetItem *_item = treeWidget->currentItem();
    if (_item->type() != RIDE_TYPE)
        return;
    RideItem *item = reinterpret_cast<RideItem*>(_item);

    QTreeWidgetItem *itemToSelect = NULL;
    for (int x=0; x<allRides->childCount(); ++x)
    {
        if (item==allRides->child(x))
        {
            if ((x+1)<allRides->childCount())
                itemToSelect = allRides->child(x+1);
            else if (x>0)
                itemToSelect = allRides->child(x-1);
            break;
        }
    }

    QString strOldFileName = item->fileName;
    allRides->removeChild(item);
    calendar->removeRide(item);
    delete item;

    QFile file(home.absolutePath() + "/" + strOldFileName);
    // purposefully don't remove the old ext so the user wouldn't have to figure out what the old file type was
    QString strNewName = strOldFileName + ".bak";

    // in case there was an existing bak file, delete it
    // ignore errors since it probably isn't there.
    QFile::remove(home.absolutePath() + "/" + strNewName);

    if (!file.rename(home.absolutePath() + "/" + strNewName))
    {
        QMessageBox::critical(
            this, "Rename Error",
            tr("Can't rename %1 to %2")
            .arg(strOldFileName).arg(strNewName));
    }

    // added djconnel: remove old cpi file, then update bests which are associated with the file
    criticalPowerWindow->deleteCpiFile(strOldFileName);

    treeWidget->setCurrentItem(itemToSelect);
    rideTreeWidgetSelectionChanged();
}

void
MainWindow::newCyclist()
{
    QDir newHome = home;
    newHome.cdUp();
    QString name = ChooseCyclistDialog::newCyclistDialog(newHome, this);
    if (!name.isEmpty()) {
        newHome.cd(name);
        if (!newHome.exists())
            assert(false);
        MainWindow *main = new MainWindow(newHome);
        main->show();
    }
}

void
MainWindow::openCyclist()
{
    QDir newHome = home;
    newHome.cdUp();
    ChooseCyclistDialog d(newHome, false);
    d.setModal(true);
    if (d.exec() == QDialog::Accepted) {
        newHome.cd(d.choice());
        if (!newHome.exists())
            assert(false);
        MainWindow *main = new MainWindow(newHome);
        main->show();
    }
}

void
MainWindow::downloadRide()
{
    (new DownloadRideDialog(this, home))->show();
}


void
MainWindow::manualRide()
{
    (new ManualRideDialog(this, home, useMetricUnits))->show();
}

const RideFile *
MainWindow::currentRide()
{
    if ((treeWidget->selectedItems().size() != 1)
        || (treeWidget->selectedItems().first()->type() != RIDE_TYPE)) {
        return NULL;
    }
    return ((RideItem*) treeWidget->selectedItems().first())->ride;
}

void
MainWindow::exportGC()
{
    if ((treeWidget->selectedItems().size() != 1)
        || (treeWidget->selectedItems().first()->type() != RIDE_TYPE)) {
        QMessageBox::critical(this, tr("Select Ride"), tr("No ride selected!"));
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export GC"), QDir::homePath(), tr("GC (*.gc)"));
    if (fileName.length() == 0)
        return;

    QString err;
    QFile file(fileName);
    GcFileReader reader;
    reader.writeRideFile(currentRide(), file);
}

void
MainWindow::exportCSV()
{
    if ((treeWidget->selectedItems().size() != 1)
        || (treeWidget->selectedItems().first()->type() != RIDE_TYPE)) {
        QMessageBox::critical(this, tr("Select Ride"), tr("No ride selected!"));
        return;
    }

    ride = (RideItem*) treeWidget->selectedItems().first();

    // Ask the user if they prefer to export with English or metric units.
    QStringList items;
    items << tr("Metric") << tr("English");
    bool ok;
    QString units = QInputDialog::getItem(
        this, tr("Select Units"), tr("Units:"), items, 0, false, &ok);
    if(!ok) 
        return;
    bool useMetricUnits = (units == items[0]);

    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export CSV"), QDir::homePath(),
        tr("Comma-Separated Values (*.csv)"));
    if (fileName.length() == 0)
        return;

    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        QMessageBox::critical(this, tr("Split Ride"), tr("The file %1 can't be opened for writing").arg(fileName));
        return;
    }

    ride->ride->writeAsCsv(file, useMetricUnits);
}

void
MainWindow::importFile()
{
    QVariant lastDirVar = settings->value(GC_SETTINGS_LAST_IMPORT_PATH);
    QString lastDir = (lastDirVar != QVariant()) 
        ? lastDirVar.toString() : QDir::homePath();
   
    const RideFileFactory &rff = RideFileFactory::instance();
    QStringList suffixList = rff.suffixes();
    suffixList.replaceInStrings(QRegExp("^"), "*.");
    QStringList fileNames; 
    QStringList allFormats;
    allFormats << QString("All Supported Formats (%1)").arg(suffixList.join(" "));
    foreach(QString suffix, rff.suffixes())
        allFormats << QString("%1 (*.%2)").arg(rff.description(suffix)).arg(suffix);
    allFormats << "All files (*.*)";
    fileNames = QFileDialog::getOpenFileNames(
        this, tr("Import from File"), lastDir,
        allFormats.join(";;"));
    if (!fileNames.isEmpty()) {
        lastDir = QFileInfo(fileNames.front()).absolutePath();
        settings->setValue(GC_SETTINGS_LAST_IMPORT_PATH, lastDir);
        QStringList fileNamesCopy = fileNames; // QT doc says iterate over a copy
        RideImportWizard *import = new RideImportWizard(fileNamesCopy, home, this);
        import->process();
    }
}

void
MainWindow::findBestIntervals()
{
    BestIntervalDialog *p = new BestIntervalDialog(this);
    p->setWindowModality(Qt::ApplicationModal); // don't allow select other ride or it all goes wrong!
    p->exec();
}

void
MainWindow::addIntervalForPowerPeaksForSecs(RideFile *ride, int windowSizeSecs, QString name)
{

    QList<const RideFilePoint*> window;
    QMap<double,double> bests;

    // don't add for intervals that are longer than the entire ride!
    if (ride->dataPoints().last()->secs < windowSizeSecs) return;

    double secsDelta = ride->recIntSecs();
    int expectedSamples = (int) floor(windowSizeSecs / secsDelta);
    double totalWatts = 0.0;

    foreach (const RideFilePoint *point, ride->dataPoints()) {
        while (!window.empty()
               && (point->secs >= window.first()->secs + windowSizeSecs)) {
            totalWatts -= window.first()->watts;
            window.takeFirst();
        }
        totalWatts += point->watts;
        window.append(point);
        int divisor = std::max(window.size(), expectedSamples);
        double avg = totalWatts / divisor;
        bests.insertMulti(avg, point->secs);
    }

    QMap<double,double> results;
    if (!bests.empty()) {
        QMutableMapIterator<double,double> j(bests);
        j.toBack();
        j.previous();
        double secs = j.value();
        results.insert(j.value() - windowSizeSecs, j.key());
        j.remove();
        while (j.hasPrevious()) {
            j.previous();
            if (abs(secs - j.value()) < windowSizeSecs)
                j.remove();
        }
    }
    QMapIterator<double,double> j(results);
    if (j.hasNext()) {
        j.next();
        double secs = j.key();
        double watts = j.value();

        QTreeWidgetItem *peak = new IntervalItem(ride, name+tr(" (%1 watts)").arg((int) round(watts)), secs, secs+windowSizeSecs, 0, 0);
        allIntervals->addChild(peak);
    }
}

void
MainWindow::findPowerPeaks()
{
    QTreeWidgetItem *which = treeWidget->selectedItems().first();
    if (which->type() != RIDE_TYPE) {
        return;
    }

    addIntervalForPowerPeaksForSecs(ride->ride, 5, "Peak 5s");
    addIntervalForPowerPeaksForSecs(ride->ride, 10, "Peak 10s");
    addIntervalForPowerPeaksForSecs(ride->ride, 20, "Peak 20s");
    addIntervalForPowerPeaksForSecs(ride->ride, 30, "Peak 30s");
    addIntervalForPowerPeaksForSecs(ride->ride, 60, "Peak 1min");
    addIntervalForPowerPeaksForSecs(ride->ride, 120, "Peak 2min");
    addIntervalForPowerPeaksForSecs(ride->ride, 300, "Peak 5min");
    addIntervalForPowerPeaksForSecs(ride->ride, 600, "Peak 10min");
    addIntervalForPowerPeaksForSecs(ride->ride, 1200, "Peak 20min");
    addIntervalForPowerPeaksForSecs(ride->ride, 1800, "Peak 30min");
    addIntervalForPowerPeaksForSecs(ride->ride, 3600, "Peak 60min");

    // now update the RideFileIntervals
    updateRideFileIntervals();
}

//----------------------------------------------------------------------
// User-define Intervals and Interval manipulation on left layout
//----------------------------------------------------------------------

void 
MainWindow::rideTreeWidgetSelectionChanged()
{
    assert(treeWidget->selectedItems().size() <= 1);
    if (treeWidget->selectedItems().isEmpty())
        ride = NULL;
    else {
        QTreeWidgetItem *which = treeWidget->selectedItems().first();
        if (which->type() != RIDE_TYPE)
            ride = NULL;
        else
            ride = (RideItem*) which;
    }
    rideSelected();

    if (!ride)
        return;

    calendar->setSelectedDate(ride->dateTime.date());

    // refresh interval list for bottom left
    // first lets wipe away the existing intervals
    QList<QTreeWidgetItem *> intervals = allIntervals->takeChildren();
    for (int i=0; i<intervals.count(); i++) delete intervals.at(i);

    // now add the intervals for the current ride
    if (ride) { // only if we have a ride pointer
        RideFile *selected = ride->ride;
        if (selected) {
            // get all the intervals in the currently selected RideFile
            QList<RideFileInterval> intervals = selected->intervals();
            for (int i=0; i < intervals.count(); i++) {
                // add as a child to allIntervals
                IntervalItem *add = new IntervalItem(selected,
                                                        intervals.at(i).name,
                                                        intervals.at(i).start,
                                                        intervals.at(i).stop,
                                                        selected->timeToDistance(intervals.at(i).start),
                                                        selected->timeToDistance(intervals.at(i).stop));
                allIntervals->addChild(add);
            }
        }
    }

	// turn off tabs that don't make sense for manual file entry
	if (ride->ride && ride->ride->deviceType() == QString("Manual CSV")) {
	    tabWidget->setTabEnabled(3,false); // Power Histogram
	    tabWidget->setTabEnabled(4,false); // PF/PV Plot
	}
   else {
	    tabWidget->setTabEnabled(3,true); // Power Histogram
	    tabWidget->setTabEnabled(4,true); // PF/PV Plot
	}
    saveAndOpenNotes();
}

void
MainWindow::showContextMenuPopup(const QPoint &pos)
{
    QTreeWidgetItem *trItem = intervalWidget->itemAt( pos );
    if (trItem != NULL && trItem->text(0) != tr("Intervals")) {
        QMenu menu(intervalWidget);

        activeInterval = (IntervalItem *)trItem;

        QAction *actRenameInt = new QAction(tr("Rename interval"), intervalWidget);
        QAction *actDeleteInt = new QAction(tr("Delete interval"), intervalWidget);
        QAction *actZoomInt = new QAction(tr("Zoom to interval"), intervalWidget);
        connect(actRenameInt, SIGNAL(triggered(void)), this, SLOT(renameInterval(void)));
        connect(actDeleteInt, SIGNAL(triggered(void)), this, SLOT(deleteInterval(void)));
        connect(actZoomInt, SIGNAL(triggered(void)), this, SLOT(zoomInterval(void)));

        if (tabWidget->currentIndex() == 1) // on ride plot
            menu.addAction(actZoomInt);
        menu.addAction(actRenameInt);
        menu.addAction(actDeleteInt);
        menu.exec(intervalWidget->mapToGlobal( pos ));
    }
}
void
MainWindow::updateRideFileIntervals()
{
    // iterate over allIntervals as they are now defined
    // and update the RideFile->intervals
    RideItem *which = (RideItem *)treeWidget->selectedItems().first();
    RideFile *current = which->ride;
    current->clearIntervals();
    for (int i=0; i < allIntervals->childCount(); i++) {
        // add the intervals as updated
        IntervalItem *it = (IntervalItem *)allIntervals->child(i);
        current->addInterval(it->start, it->stop, it->text(0));
    }

    // emit signal for interval data changed
    intervalsChanged();

    // set dirty
    which->setDirty(true);
}

void
MainWindow::deleteInterval() {
    int index = allIntervals->indexOfChild(activeInterval);
    delete allIntervals->takeChild(index);
    updateRideFileIntervals(); // will emit intervalChanged() signal
}

void
MainWindow::renameInterval() {
    // go edit the name
    activeInterval->setFlags(activeInterval->flags() | Qt::ItemIsEditable);
    intervalWidget->editItem(activeInterval, 0);
}

void
MainWindow::intervalEdited(QTreeWidgetItem *, int) {
    // the user renamed the interval
    updateRideFileIntervals(); // will emit intervalChanged() signal
}

void
MainWindow::zoomInterval() {
    // zoom into this interval on allPlot
    allPlotWindow->zoomInterval(activeInterval);
}

void
MainWindow::intervalTreeWidgetSelectionChanged()
{
    intervalSelected();
}

void MainWindow::getBSFactors(float &timeBS, float &distanceBS)
{

    int rides;
    double seconds, distance, bs;
    RideItem * lastRideItem;
    QProgressDialog * progress;
    bool aborted = false;
    seconds = rides = 0;
    distance = bs = 0;
    timeBS = distanceBS = 0.0;

    QVariant BSdays = settings->value(GC_BIKESCOREDAYS);
    if (BSdays.isNull() || BSdays.toInt() == 0)
	BSdays.setValue(30); // by default look back no more than 30 days

    // if there are rides, find most recent ride so we count back from there:
    if (allRides->childCount() > 0)
	lastRideItem =  (RideItem*) allRides->child(allRides->childCount() - 1);
    else
	lastRideItem = ride; // not enough rides, use current ride

    // set up progress bar
    progress = new QProgressDialog(QString(tr("Computing bike score estimating factors.\n")),
	tr("Abort"),0,BSdays.toInt(),this);
    int endingOffset = progress->labelText().size();
    
    for (int i = 0; i < allRides->childCount(); ++i) {
	RideItem *item = (RideItem*) allRides->child(i);
	int days =  item->dateTime.daysTo(lastRideItem->dateTime);
        if (
	    (item->type() == RIDE_TYPE) &&
	    // (item->ride) &&
	    (days  >= 0) && 
	    (days < BSdays.toInt())  
	    ) {

	    RideMetric *m;
	    item->computeMetrics();

	    QString existing = progress->labelText();
            existing.chop(progress->labelText().size() - endingOffset);
            progress->setLabelText(
               existing + QString(tr("Processing %1...")).arg(item->fileName));


	    // only count rides with BS > 0
            if ((m = item->metrics.value("skiba_bike_score")) &&
		    m->value(true)) {
		bs += m->value(true);

		if ((m = item->metrics.value("time_riding"))) {
		    seconds += m->value(true);
		}

		if ((m = item->metrics.value("total_distance"))) {
		    distance += m->value(true);
		}

		rides++;
	    }
	    // check progress
	    QCoreApplication::processEvents();
            if (progress->wasCanceled()) {
		aborted = true;
                    goto done;
	    }
	    // set progress from 0 to BSdays
            progress->setValue(BSdays.toInt() - days);

        }
    }
    if (rides) {
	if (!useMetricUnits)
            distance *= MILES_PER_KM;
	timeBS = (bs * 3600) / seconds;  // BS per hour
	distanceBS = bs / distance;  // BS per mile or km
    }
done:
    if (aborted) {
	timeBS = distanceBS = 0;
    }

    delete progress;
}

void
MainWindow::saveAndOpenNotes()
{
    // First save the contents of the notes window.
    saveNotes();

    // Now open any notes associated with the new ride.
    rideNotes->setPlainText("");
    QString notesPath = home.absolutePath() + "/" + ride->notesFileName;
    QFile notesFile(notesPath);

    if (notesFile.exists()) {
        if (notesFile.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream in(&notesFile);
            rideNotes->setPlainText(in.readAll());
            notesFile.close();
        }
        else {
            QMessageBox::critical(
                this, tr("Read Error"),
                tr("Can't read notes file %1").arg(notesPath));
        }
    }

    currentNotesFile = ride->notesFileName;
    currentNotesChanged = false;
}

void MainWindow::saveNotes() 
{
    if ((currentNotesFile != "") && currentNotesChanged) {
        QString notesPath = 
            home.absolutePath() + "/" + currentNotesFile;
        QString tmpPath = notesPath + ".tmp";
        QFile tmp(tmpPath);
        if (tmp.open(QFile::WriteOnly | QFile::Truncate)) {
            QTextStream out(&tmp);
            out << rideNotes->toPlainText();
            tmp.close();
            QFile::remove(notesPath);
            if (rename(tmpPath.toAscii().constData(),
                       notesPath.toAscii().constData()) == -1) {
                QMessageBox::critical(
                    this, tr("Write Error"),
                    tr("Can't rename %1 to %2")
                    .arg(tmpPath).arg(notesPath));
            }
        }
        else {
            QMessageBox::critical(
                this, tr("Write Error"),
                tr("Can't write notes file %1").arg(tmpPath));
        }
    }
}

void 
MainWindow::resizeEvent(QResizeEvent*)
{
    settings->setValue(GC_SETTINGS_MAIN_GEOM, geometry());
}

void 
MainWindow::showOptions()
{
    ConfigDialog *cd = new ConfigDialog(home, zones_, this);
    cd->exec();
    zonesChanged();
}

void 
MainWindow::moveEvent(QMoveEvent*)
{
    settings->setValue(GC_SETTINGS_MAIN_GEOM, geometry());
}

void
MainWindow::closeEvent(QCloseEvent* event)
{
    if (saveRideExitDialog() == false) event->ignore();
    saveNotes();
}

void
MainWindow::leftLayoutMoved()
{
    settings->setValue(GC_SETTINGS_CALENDAR_SIZES, leftLayout->saveState());
}

void
MainWindow::splitterMoved()
{
    settings->setValue(GC_SETTINGS_SPLITTER_SIZES, splitter->saveState());
}

// set the rider value of CP to the value derived from the CP model extraction
void
MainWindow::setCriticalPower(int cp)
{
  // determine in which range to write the value: use the range associated with the presently selected ride
  int range;
  if (ride)
      range = ride->zoneRange();
  else {
      QDate today = QDate::currentDate();
      range = zones_->whichRange(today);
  }

  // add a new range if we failed to find a valid one
  if (range < 0) {
    // create an infinite range
    zones_->addZoneRange();
    range = 0;
  }

  zones_->setCP(range, cp);        // update the CP value
  zones_->setZonesFromCP(range);   // update the zones based on the value of CP
  zones_->write(home);             // write the output file

  QDate startDate = zones_->getStartDate(range);
  QDate endDate   =  zones_->getEndDate(range);
  QMessageBox::information(
			   this,
			   tr("CP saved"),
			   tr("Range from %1 to %2\nRider CP set to %3 watts") .
			   arg(startDate.isNull() ? "BEGIN" : startDate.toString()) .
			   arg(endDate.isNull() ? "END" : endDate.toString()) .
			   arg(cp)
			   );
  zonesChanged();
}

void
MainWindow::tabChanged(int index)
{
    criticalPowerWindow->setActive(index == 2);
    performanceManagerWindow->setActive(index == 6);
}

void
MainWindow::aboutDialog()
{
    QMessageBox::about(this, tr("About GoldenCheetah"), tr(
            "<center>"
            "<h2>GoldenCheetah</h2>"
            "<i>Cycling Power Analysis Software for Linux, Mac, and Windows</i>"
            "<p><i>Build date: "
            "") + QString(__DATE__) + " " + QString(__TIME__) + "</i>"
            "<p><i>Version: " + QString(GC_VERSION) + ("</i>"
            "<p>GoldenCheetah is licensed under the "
            "<a href=\"http://www.gnu.org/copyleft/gpl.html\">GNU General "
            "Public License</a>."
            "<p>Source code can be obtained from "
            "<a href=\"http://goldencheetah.org/\">"
            "http://goldencheetah.org/</a>."
            "</center>"
            ));
}


void MainWindow::importRideToDB()
{
    MetricAggregator aggregator;
    aggregator.aggregateRides(home, zones());
}

void MainWindow::scanForMissing()
{
    MetricAggregator aggregator;
    aggregator.scanForMissing(home, zones());
}



void
MainWindow::notesChanged()
{
    currentNotesChanged = true;
}

void MainWindow::showTools()
{
   ToolsDialog *td = new ToolsDialog();
   td->show();
}

void
MainWindow::saveRide()
{
    saveRideSingleDialog(ride); // will update Dirty flag if saved
}

void
MainWindow::splitRide()
{
    (new SplitRideDialog(this))->exec();
}

void
MainWindow::deleteRide()
{
    QTreeWidgetItem *_item = treeWidget->currentItem();
    if (_item==NULL || _item->type() != RIDE_TYPE)
        return;
    RideItem *item = reinterpret_cast<RideItem*>(_item);
    QMessageBox msgBox;
    msgBox.setText(tr("Are you sure you want to delete the ride:"));
    msgBox.setInformativeText(item->fileName);
    QPushButton *deleteButton = msgBox.addButton(tr("Delete"),QMessageBox::YesRole);
    msgBox.setStandardButtons(QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.exec();
    if(msgBox.clickedButton() == deleteButton)
        removeCurrentRide();
}

/*
 *  This slot gets called when the user picks a new date, using the mouse,
 *  in the calendar.  We have to adjust TreeView to match.
 */
void MainWindow::dateChanged(const QDate &date)
{
    for (int i = 0; i < allRides->childCount(); i++)
    {
        ride = (RideItem*) allRides->child(i);
        if (ride->dateTime.date() == date) {
            treeWidget->scrollToItem(allRides->child(i),
                QAbstractItemView::EnsureVisible);
            treeWidget->setCurrentItem(allRides->child(i));
            i = allRides->childCount();
        }
    }
}

