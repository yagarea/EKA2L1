/*
 * main.cpp
 *
 *  Created on: Feb 21, 2019
 *      Author: fewds
 */

#include <e32base.h>
#include <e32std.h>
#include <e32cmn.h>
#include <w32std.h>
#include <e32debug.h>
#include <eikon.hrh>

class CActiveInputHandler: public CActive
{
  RWsSession *session;
  RWindowGroup *group;
  
  TWsEvent event;
  
public:
  explicit CActiveInputHandler(RWsSession *session, RWindowGroup *group)
    : CActive(EPriorityStandard), session(session), group(group) 
  {
    CActiveScheduler::Add(this);
  }
  
  void StartReceiveInput()
  {
    Cancel();
    session->EventReady(&iStatus);
    SetActive();
  }
  
  void RunL() 
  {
    session->GetEvent(event);
    
    switch (event.Type())
    {
    case EEventKeyDown: case EEventKeyUp:
    {
      TKeyEvent *keyEvt = event.Key();
      if (keyEvt->iScanCode == 197)
      {
        RDebug::Printf("Red key pressed down/up");
        
        // STOP IMMIDIATELY
        CActiveScheduler::Stop(); 
        return;
      } else if (keyEvt->iScanCode == 180)
      {
        RDebug::Printf("Mid button pressed down/up"); 
      } else if (keyEvt->iScanCode == 196)
      {
        RDebug::Printf("Green key pressed down/up"); 
      }

      group->CaptureKeyUpAndDowns(EKeyOK, 0, 0);

      break;
    }
    
    case EEventKey:
    {
      TKeyEvent *keyEvt = event.Key();
      
      if (keyEvt->iScanCode == 197)
      {
        RDebug::Printf("Red key pressed down/up");
        
        // STOP IMMIDIATELY
        CActiveScheduler::Stop(); 
        return;
      } 

      group->CaptureKey(EKeyOK, 0, 0);
      break;
    }
    
    case EEventFocusGained:
    {
      RDebug::Printf("Focus gained!");
      break;
    }
    
    case EEventFocusLost:
    {
      RDebug::Printf("Focus lost!");
      break;
    }
    
    default:
    {
      break;
    }
    }
    
    session->EventReady(&iStatus);
    SetActive();
  }
  
  void DoCancel()
  {
    session->EventReadyCancel();
  }
};

void MainL()
{
  // Setup the active scheduler, push it up
  CActiveScheduler *sched = new (ELeave) CActiveScheduler();
  CleanupStack::PushL(sched);
  
  // Install the scheduler
  CActiveScheduler::Install(sched);
  
  RFs fs;
  User::LeaveIfError(fs.Connect(-1));
  
  fs.SetSessionToPrivate(EDriveC);
  
  RWsSession winSession;
  User::LeaveIfError(winSession.Connect(fs));
  
  CWsScreenDevice *device = new (ELeave) CWsScreenDevice(winSession);
  CleanupStack::PushL(device);
  device->Construct();
  
  RWindowGroup *group = new (ELeave) RWindowGroup(winSession);
  CleanupStack::PushL(group);
  
  // Capture key handle. Don't need that now
  group->Construct(0, true);
  group->CaptureKeyUpAndDowns(EKeyOK, 0, 0);
  group->CaptureKey(EKeyOK, 0, 0);
  
  CActiveInputHandler *handler = new (ELeave) CActiveInputHandler(&winSession, group);
  CleanupStack::PushL(handler);
  
  handler->StartReceiveInput();
  CActiveScheduler::Start();
  
  CleanupStack::PopAndDestroy(4);
}

TInt E32Main()
{ 
  CTrapCleanup::New();
  TRAPD(err, MainL());
  
  return err;
}
