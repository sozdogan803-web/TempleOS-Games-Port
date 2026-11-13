//Native-port boot: the parts of KMain/SysGlblsInit/SysGrInit that apply
//without real hardware.  Runs as Adam right after the kernel files.
//The C runtime already made Fs=Gs->seth_task=adam and ran TaskInit on it.

I64 tos_boot_i,tos_boot_j;
CCmpGlbls cmp; //Compiler.PRJ defines it; only the lexer part of the compiler is here

mp_cnt=1; //one CPU: games split work by mp_cnt (Talons)
mem_physical_space=mem_mapped_space=0x80000000; //ExpandBuf rejects sizes above it (Stadium's GR)

TosDefinesLoad;

CmpLoadDefines;
CmpFillTables;

U0 TosAsmHashLoad()
{//AsmHashLoad without the assembler: registers and keywords only, so Lex
//knows #include/#define (MenuFile, DolDoc).  OPCODE entries are skipped.
  I64 i;
  CHashGeneric *tmph;
  CHashReg *tmpr;
  CCmpCtrl *cc;
  cmp.asm_hash=HashTableNew(1024);
  cc=CmpCtrlNew(FileRead("OpCodes.DD"),,"OpCodes.DD.Z");
  cc->htc.hash_table_lst=NULL;
  Lex(cc);
  while (cc->token) {
    if (cc->token!=TK_IDENT)
      LexExcept(cc,"Expecting identifier at ");
    i=LstMatch(cc->cur_str,"NONE\0R8\0R16\0R32\0R64\0SEG\0FSTK\0"
	  "MM\0XMM\0OPCODE\0KEYWORD\0ASM_KEYWORD\0");
    Lex(cc); //skip stmt kind
    if (REGT_R8<=i<=REGT_XMM || i==10 || i==11) {
      if (i>=10)
	tmph=CAlloc(sizeof(CHashGeneric));
      else
	tmph=tmpr=CAlloc(sizeof(CHashReg));
      tmph->str=cc->cur_str;
      cc->cur_str=NULL;
      if (Lex(cc)!=TK_I64)
	LexExcept(cc,"Expecting int at ");
      if (i==10) {
	tmph->type=HTT_KEYWORD;
	tmph->user_data0=cc->cur_i64;
      } else if (i==11) {
	tmph->type=HTT_ASM_KEYWORD;
	tmph->user_data0=cc->cur_i64;
      } else {
	tmpr->type=HTT_REG;
	tmpr->reg_type=i;
	tmpr->reg_num=cc->cur_i64;
      }
      HashAdd(tmph,cmp.asm_hash);
      Lex(cc);
    }
    while (cc->token && cc->token!=';')
      Lex(cc);
    Lex(cc); //Skip ';'
  }
  CmpCtrlDel(cc);
  adam_task->hash_table->next=cmp.asm_hash;
}
TosAsmHashLoad;

LBts(&sys_run_level,RLf_VGA);

sys_cache_line_width=64;
sys_focus_task=Fs;
QueInit(&sys_macro_head);
blkdev.dft_iso_filename	=AStrNew(DFT_ISO_FILENAME);
blkdev.dft_iso_c_filename=AStrNew(DFT_ISO_C_FILENAME);
blkdev.tmp_filename	=AStrNew("~/Tmp.DD.Z");
rev_bits_table=CAlloc(256);
set_bits_table=CAlloc(256);
for (tos_boot_i=0;tos_boot_i<256;tos_boot_i++)
  for (tos_boot_j=0;tos_boot_j<8;tos_boot_j++) {
    if (Bt(&tos_boot_i,7-tos_boot_j))	LBts(rev_bits_table+tos_boot_i,tos_boot_j);
    if (Bt(&tos_boot_i,tos_boot_j))	set_bits_table[tos_boot_i]++;
  }
ext=CAlloc(EXT_EXTS_NUM*sizeof(U8 *));
fp_getstr2=&SysGetStr2;
KeyDevInit;
pow10_I64=CAlloc(sizeof(F64)*(308+308+1));
for (tos_boot_i=-308;tos_boot_i<309;tos_boot_i++)
  pow10_I64[tos_boot_i+309]=Pow10(tos_boot_i);
QueInit(&scrncast.snd_head);
scrncast.t0_now=Now;
scrncast.t0_tS=tS;
ProgressBarsRst;
QueInit(&dev.pci_head);
dbg.fun_seg_cache=CAlloc(FUN_SEG_CACHE_SIZE*sizeof(CFunSegCache));

//SysGrInit, 640x480 16 color branch
text.font=sys_font_std;
text.aux_font=sys_font_cyrillic;
text.vga_alias	=dev.uncached_alias+VGAM_GRAPHICS;
text.vga_text_alias	=dev.uncached_alias+VGAM_TEXT;
text.cols=GR_WIDTH/FONT_WIDTH;
text.rows=GR_HEIGHT/FONT_HEIGHT;
MemSet(text.vga_alias,0,GR_WIDTH*GR_HEIGHT>>3);
text.raw_scrn_image=CAlloc(GR_WIDTH*GR_HEIGHT/8);
text.border_chars[2] (I64)=0x0908070605040302;
text.border_chars[10](U32)=0x0D0C0B0A;

StrCpy(Fs->task_name,"Adam Task CPU00");
StrCpy(Fs->task_title,Fs->task_name);
Fs->title_src=TTS_TASK_NAME;
Fs->win_right=text.cols-2;
Fs->win_top++;
TaskDerivedValsUpdate;
SysDefinesLoad;

//Keyboard and mouse arrive from the host window, not from 8042 IRQs.
KbdMsInit;
MsInit;
kbd.irqs_working=TRUE;
ms_hard.installed=TRUE;
ms_hard.install_attempts=1;
ms_hard.irqs_working=TRUE;
ms_hard.evt=TRUE; //WinMgr copies the centered ms_hard.pos into ms.pos
LBts(&sys_run_level,RLf_INTERRUPTS);
