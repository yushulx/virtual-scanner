#ifndef TWAIN_APP_QT_H
#define TWAIN_APP_QT_H

#include <QtWidgets/QMainWindow>
#include "ui_twain_app_qt.h"
#include "TWAINSession.h"

class MyMsgEventFilter;
class TWAIN_App_QT : public QMainWindow, public CTWAINSession, public QScriptable
{
  Q_OBJECT

  public:
  TWAIN_App_QT(QScriptEngine *pEngine, QWidget *parent = 0, Qt::WindowFlags flags = 0);
  ~TWAIN_App_QT();

  public slots:
  QStringList GetSourceList();
  QString GetCurrentSource();
  bool SetCurrentSource(QString strNewSource);
  bool LowerStateTo(int nState);
  int GetCurrentState();
  void AppendScriptStatusText(QString strText);

  signals:
  void customDataSourceSignal();
  void customAppendStatusText(QString &strText);

  protected:
  /**
  * Sends trace messages to the Status window
  * @param[in] pszFormat a printf style format string
  * @param[in] ... stacked printf style parameters
  */
  virtual void TraceMessage(const char *pszFormat, ...);
  /**
  * Examines the status and prepares the Application for appropriate action
  * @param[in] twStatus a reference to a structure containing the status of the recently failed method
  */
  virtual void OnError(const TW_STATUS &twStatus);
  /**
  * Saves the native image to bmp file in the desired location
  * @param[in] pData OS Dependent pointer/handle to native image data
  * @param[out] bAbort default value false, set to true if abort transfer is desired
  * @param[out] bFree default value is true, decides if pData should be freed by underlying transfer code
  */
  virtual void OnNativeTransfer(const TW_MEMREF &pData, bool &bAbort, bool &bFree);
  /**
  * Creates an image / .bmp for uncompressed / .jpg for jpg compressed and .tif for G4 in the desired location
  * @param[in] twData structuring containing information about this memory transfer data
  * @param[in] twRC either TWRC_SUCCESS meaning the current image is not finished or TWRC_XFERDONE meaning this is the last block
  * @param[out] bAbort default value false, set to true if abort transfer is desired
  */
  virtual void OnMemoryTransfer(const TW_IMAGEINFO &twInfo, const TW_IMAGEMEMXFER &twData, const TW_UINT16 &twRC, bool &bAbort);
  /**
  * Copies the resulting file to the desired location
  * @param[in] strPath complete path to the resulting image
  * @param[out] bAbort default value false, set to true if abort transfer is desired
  */
  virtual void OnFileTransfer(const TW_SETUPFILEXFER &twFileXfer, bool &bAbort);
  /**
  * Saves the file in memory to a file in the desired location in the expected file format
  * @param[in] twData structuring containing information about this memory transfer data
  * @param[in] twRC either TWRC_SUCCESS meaning the current image is not finished or TWRC_XFERDONE meaning this is the last block
  * @param[out] bAbort default value false, set to true if abort transfer is desired
  */
  virtual void OnFileMemTransfer(const TW_IMAGEMEMXFER &twData, const TW_UINT16 &twRC, bool &bAbort);
  /**
  * Optional Implementation specific handler called when an event is signaled from the DS.
  * Do very little on this method as it may be in the context of a thread other than the TWAIN session.
  */
  virtual void OnSignalDSRequest();
  /**
  * Implementation checks for lost signals
  */
  virtual void PostDSMCall();

  virtual void OnImageBegin(const TW_IMAGEINFO &twInfo, TW_UINT16 twXferMech);
  virtual void OnImageEnd(const TW_IMAGEINFO &twInfo, TW_UINT16 twXferMech);

  void DoSetCapability(QTreeWidgetItem *pItem);

  virtual void closeEvent(QCloseEvent *pEvent);

  /**
  * Method enables / disables controls based on the current TWAIN state
  */
  void EnableGUIByCurrentState();
  /**
  * Populates the DataSource combobox
  */
  void PopulateDSList();
  void PopulateTripletList();
  void PopulateCapabilityList();
  void RefreshCapabilityList(bool bUpdateAll=false);
  void FillItemFromCapability(QTreeWidgetItem *pItem);
  void FillExtImageInfoEdit();

  void OnPendingXfers(TW_UINT16 twMsg);
  void OnSetupMemXfer(TW_UINT16 twMsg);
  void OnSetupFileXfer(TW_UINT16 twMsg);
  void OnStatus(TW_UINT16 twMsg);
  void OnXferGroup(TW_UINT16 twMsg);
  void OnCustomDSData(TW_UINT16 twMsg);
  void OnDeviceEvent(TW_UINT16 twMsg);
  void OnFileSystem(TW_UINT16 twMsg);
  void OnPassThru(TW_UINT16 twMsg);
  void OnStatusUTF8(TW_UINT16 twMsg);
  void OnImageLayout(TW_UINT16 twMsg);
  void OnCIEColor(TW_UINT16 twMsg);
  void OnGrayResponse(TW_UINT16 twMsg);
  void OnRGBResponse(TW_UINT16 twMsg);
  void OnJPEGCompression(TW_UINT16 twMsg);
  void OnPalette8(TW_UINT16 twMsg);
  void OnExtImageInfo(TW_UINT16 twMsg);
  void OnICCProfile(TW_UINT16 twMsg);

  pTW_EXTIMAGEINFO CreateExtImageInfo(const vector<TW_UINT16> &lstExtIInfo);
  void CleanupExtImageInfo(pTW_EXTIMAGEINFO &pInfo);
  pTW_INFO FindInfo(pTW_EXTIMAGEINFO pInfo, TW_UINT16 twInfoID);

  void TraceStruct(const TW_PENDINGXFERS &twData);
  void TraceStruct(const TW_SETUPMEMXFER &twData);
  void TraceStruct(const TW_SETUPFILEXFER &twData);
  void TraceStruct(const TW_STATUS &twData);
  void TraceStruct(const DWORD &twData);
  void TraceStruct(const TW_CUSTOMDSDATA &twData);
  void TraceStruct(const TW_DEVICEEVENT &twData);
  void TraceStruct(const TW_FILESYSTEM &twData);
  void TraceStruct(const TW_PASSTHRU &twData);
  void TraceStruct(const TW_STATUSUTF8 &twData);
  void TraceStruct(const TW_IMAGELAYOUT &twData);
  void TraceStruct(const TW_CIECOLOR &twData);
  void TraceStruct(const TW_GRAYRESPONSE &twData);
  void TraceStruct(const TW_RGBRESPONSE &twData);
  void TraceStruct(const TW_PALETTE8 &twData);
  void TraceStruct(const TW_EXTIMAGEINFO &twData);
  void TraceStruct(const TW_MEMORY &twData);

  vector<TW_UINT16> m_lstExtImageInfo; /**< list of extended image info structures expected to be dispatched after a transfer */
  bool m_bSignalCalledInTWAIN; /**< flag that indicates a signal was called at a time when processing was not possible */
  int m_nImageCount;

  QFile *m_pMemoryFile;
  QDataStream *m_pMemoryDataStream;
  TW_UINT16 m_twMemoryFileFormat;
  BITMAPFILEHEADER m_bmfhBitmap; /**< used when in memory transfer mode and the output format is .bmp */
  BITMAPINFOHEADER m_bmihBitmap; /**< used when in memory transfer mode and the output format is .bmp */
  int m_nPaletteOffset; /**< used when in memory transfer mode and the output format is .bmp */

  TW_CUSTOMDSDATA m_twCustomDSData;
  TW_SETUPMEMXFER m_twSetupMemXfer;
  TW_SETUPFILEXFER m_twSetupFileXfer;
  TW_PENDINGXFERS m_twPendingXfers;
  TW_STATUS m_twStatus;
  TW_FILESYSTEM m_twFileSystem;
  TW_DEVICEEVENT m_twDeviceEvent;
  TW_IMAGELAYOUT m_twImageLayout;
  
  protected slots:
  void DataSourceSignal();
  void AppendStatusText(QString &strText);
  //void on_<widget name>_<signal name>(<signal parameters>);
  void on_treCapability_itemClicked(QTreeWidgetItem * item, int column);
  void on_treCapability_itemDoubleClicked(QTreeWidgetItem * item, int column);
  void on_treTriplet_itemDoubleClicked(QTreeWidgetItem * item, int column);
  void on_treCapability_itemActivated(QTreeWidgetItem *pItem, int nColumn);
  void on_btnLoadDSM_clicked(bool bChecked);
  void on_btnUnloadDSM_clicked(bool bChecked);
  void on_btnOpenDSM_clicked(bool bChecked);
  void on_btnCloseDSM_clicked(bool bChecked);
  void on_btnOpen_clicked(bool bChecked);
  void on_btnClose_clicked(bool bChecked);
  void on_btnSet_clicked(bool bChecked);
  void on_btnEnable_clicked(bool bChecked);
  void on_btnDisable_clicked(bool bChecked);
  void on_btnRecover_clicked(bool bChecked);
  void on_btnSetDefault_clicked(bool bChecked);
  void on_btnTransfer_clicked(bool bChecked);
  void on_btnNext_clicked(bool bChecked);
  void on_btnAbort_clicked(bool bChecked);
  void on_radAuto_toggled(bool bChecked);
  void on_radManual_toggled(bool bChecked);
  void on_btnSelPath_clicked(bool bChecked);
  void on_mnuLoadFromState1_triggered(bool bChecked);
  void on_mnuLoadFromCurrentState_triggered(bool bChecked);
  void on_btnAllSupported_clicked(bool bChecked);

//mnuLoadFromCurrentState

  //Check boxes
  void on_chkExtImageInfo_clicked(bool bChecked);
  void on_chkSave_clicked(bool bChecked);

  private:
  Ui::TWAIN_App_QTClass ui;
  QScriptEngine *m_pEngine;
};

class CQTWAINApp : public QApplication
{
  public:
  CQTWAINApp(int &argc, char **argv, int = QT_VERSION);
  virtual ~CQTWAINApp();
  TWAIN_App_QT *m_pMainWnd;
  MyMsgEventFilter *m_pEventFilter;
};

#endif // TWAIN_APP_QT_H
