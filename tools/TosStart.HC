//Native-port start-up: StartOS.HC's Adam set-up, then one user task that
//runs the translated game instead of a command line.

extern U0 TosGameMain(); //C: runs the game file's top-level code
extern U0 TosQuit(Bool by_except); //C: exits the process (by_except: report Fs->except_ch)

U0 TosUserTask(U8 *)
{//What UserCmdLine+UserStartUp do, minus the shell.
  Fs->win_inhibit=WIG_USER_TASK_DFT;
  DocTermNew;
  LBts(&Fs->display_flags,DISPLAYf_SHOW);
  WinToTop;
  WinZBufUpdate;
  Bool by_except=FALSE;
  try
    TosGameMain;
  catch {
    by_except=TRUE;
    Fs->catch_except=TRUE;
  }
  TosQuit(by_except);
}

DocTermNew;
WinVert(2,10);
sys_winmgr_task=Spawn(&WinMgrTask,NULL,"Window Mgr");
Fs->win_inhibit=WIG_TASK_DFT-WIF_SELF_BORDER
	-WIF_SELF_GRAB_SCROLL-WIF_SELF_CTRLS;
LBts(&Fs->display_flags,DISPLAYf_CHILDREN_NOT_ON_TOP);
//Adam keeps no window on screen: the game is the only program here.
LBts(&sys_run_level,RLf_HOME);
sys_focus_task=Spawn(&TosUserTask,NULL,"Game");

U0 SrvStartUp()
{//HomeSys.HC's: run each time a servant task (PopUp) is spawned.
  DocTermNew;
  LBts(&Fs->display_flags,DISPLAYf_SHOW);
  WinToTop;
  WinZBufUpdate;
}
