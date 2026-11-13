//Keyboard.HC's asm DU8 scan code tables, restated as arrays (the translator skips asm blocks).

U8 NORMAL_KEY_SCAN_DECODE_TABLE[0x50]={
  0,CH_ESC,'1','2','3','4','5','6','7','8','9','0','-','=',CH_BACKSPACE,'\t',
  'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
  'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
  'b','n','m',',','.','/',0,'*',0,CH_SPACE,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,'-',0,0,0,'+',0};

U8 SHIFT_KEY_SCAN_DECODE_TABLE[0x50]={
  0,CH_SHIFT_ESC,'!','@','#','$$','%','^','&','*','(',')','_','+',CH_BACKSPACE,'\t',
  'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
  'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
  'B','N','M','<','>','?',0,'*',0,CH_SHIFT_SPACE,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,'-',0,0,0,'+',0};

U8 CTRL_KEY_SCAN_DECODE_TABLE[0x50]={
  0,CH_ESC,'1','2','3','4','5','6','7','8','9','0','-','=',CH_BACKSPACE,'\t',
  CH_CTRLQ,CH_CTRLW,CH_CTRLE,CH_CTRLR,CH_CTRLT,CH_CTRLY,CH_CTRLU,
  CH_CTRLI,CH_CTRLO,CH_CTRLP,'[',']','\n',0,CH_CTRLA,CH_CTRLS,
  CH_CTRLD,CH_CTRLF,CH_CTRLG,CH_CTRLH,CH_CTRLJ,CH_CTRLK,CH_CTRLL,
  ';','\'','`',0,'\\',CH_CTRLZ,CH_CTRLX,CH_CTRLC,CH_CTRLV,
  CH_CTRLB,CH_CTRLN,CH_CTRLM,',','.','/',0,'*',0,CH_SPACE,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,'-',0,0,0,'+',0};
