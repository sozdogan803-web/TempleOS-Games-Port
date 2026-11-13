//LexExpression for the native port.  TempleOS compiles the expression to
//machine code and calls it; here a small evaluator handles the constant
//expressions menus and DolDoc use (numbers, chars, #defines, operators).
//shortcut: no variables, calls or casts; a game needing them gets a TosExeErr.

extern U0 TosDefinesLoad(); //C runtime: the translator's #define table -> Adam's hash table

I64 TosOpLvl(I64 tk)
{//HolyC binary precedence, 1=tightest.  0=not a binary operator.
  switch (tk) {
    case '`': case TK_SHL: case TK_SHR:			return 1;
    case '*': case '/': case '%':			return 2;
    case '&':						return 3;
    case '^':						return 4;
    case '|':						return 5;
    case '+': case '-':					return 6;
    case '<': case '>': case TK_LESS_EQU: case TK_GREATER_EQU: return 7;
    case TK_EQU_EQU: case TK_NOT_EQU:			return 8;
    case TK_AND_AND:					return 9;
    case TK_XOR_XOR:					return 10;
    case TK_OR_OR:					return 11;
  }
  return 0;
}

F64 TosAsF64(I64 v,Bool is_f)
{
  if (is_f)
    return v(F64);
  return ToF64(v);
}

U0 TosExeErr(CCmpCtrl *cc,U8 *msg)
{//Not LexExcept: it calls Dbg while RLf_ADAM_SERVER is clear, which the port never sets.
//Callers that expect bad input (GetI64) catch this; elsewhere it stops the game with a report.
  no_warn cc,msg;
  throw('Compiler');
}

I64 TosExprBin(CCmpCtrl *cc,Bool *_f,I64 max_lvl);

I64 TosExprUnary(CCmpCtrl *cc,Bool *_f)
{
  I64 res=0;
  F64 d;
  *_f=FALSE;
  switch (cc->token) {
    case TK_I64:
    case TK_CHAR_CONST:
      res=cc->cur_i64;
      Lex(cc);
      break;
    case TK_F64:
      d=cc->cur_f64;
      res=d(I64);
      *_f=TRUE;
      Lex(cc);
      break;
    case '(':
      Lex(cc);
      res=TosExprBin(cc,_f,11);
      if (cc->token!=')')
	TosExeErr(cc,"Missing ')' at ");
      Lex(cc);
      break;
    case '+':
      Lex(cc);
      res=TosExprUnary(cc,_f);
      break;
    case '-':
      Lex(cc);
      res=TosExprUnary(cc,_f);
      if (*_f) {
	d=-res(F64);
	res=d(I64);
      } else
	res=-res;
      break;
    case '~':
      Lex(cc);
      res=~TosExprUnary(cc,_f);
      *_f=FALSE;
      break;
    case '!':
      Lex(cc);
      res=!TosExprUnary(cc,_f);
      *_f=FALSE;
      break;
    default:
      TosExeErr(cc,"Expression not supported in native port at ");
  }
  return res;
}

I64 TosExprBin(CCmpCtrl *cc,Bool *_f,I64 max_lvl)
{
  I64 a,b,op,lvl;
  Bool bf;
  F64 x,y,z;
  a=TosExprUnary(cc,_f);
  while ((lvl=TosOpLvl(op=cc->token)) && lvl<=max_lvl) {
    Lex(cc);
    if (op=='`') //right associative
      b=TosExprBin(cc,&bf,lvl);
    else
      b=TosExprBin(cc,&bf,lvl-1);
    if ((*_f || bf) && (op=='+' || op=='-' || op=='*' || op=='/' || op=='`' ||
	  op=='<' || op=='>' || op==TK_LESS_EQU || op==TK_GREATER_EQU ||
	  op==TK_EQU_EQU || op==TK_NOT_EQU)) {
      x=TosAsF64(a,*_f);
      y=TosAsF64(b,bf);
      *_f=TRUE;
      switch (op) {
	case '+': z=x+y; break;
	case '-': z=x-y; break;
	case '*': z=x*y; break;
	case '/': z=x/y; break;
	case '`': z=Pow(x,y); break;
	default:
	  *_f=FALSE;
	  switch (op) {
	    case '<':		a=x<y;	break;
	    case '>':		a=x>y;	break;
	    case TK_LESS_EQU:	a=x<=y;	break;
	    case TK_GREATER_EQU: a=x>=y; break;
	    case TK_EQU_EQU:	a=x==y;	break;
	    case TK_NOT_EQU:	a=x!=y;	break;
	  }
      }
      if (*_f)
	a=z(I64);
    } else {
      if (*_f) a=ToI64(a(F64));
      if (bf) b=ToI64(b(F64));
      *_f=FALSE;
      switch (op) {
	case '`':		a=a`b;		break;
	case TK_SHL:		a=a<<b;		break;
	case TK_SHR:		a=a>>b;		break;
	case '*':		a=a*b;		break;
	case '/':		if (b) a=a/b;	break;
	case '%':		if (b) a=a%b;	break;
	case '&':		a=a&b;		break;
	case '^':		a=a^b;		break;
	case '|':		a=a|b;		break;
	case '+':		a=a+b;		break;
	case '-':		a=a-b;		break;
	case '<':		a=a<b;		break;
	case '>':		a=a>b;		break;
	case TK_LESS_EQU:	a=a<=b;		break;
	case TK_GREATER_EQU:	a=a>=b;		break;
	case TK_EQU_EQU:	a=a==b;		break;
	case TK_NOT_EQU:	a=a!=b;		break;
	case TK_AND_AND:	a=a&&b;		break;
	case TK_XOR_XOR:	a=a^^b;		break;
	case TK_OR_OR:		a=a||b;		break;
      }
    }
  }
  return a;
}

I64 LexExpression(CCmpCtrl *cc)
{//Eval cur expression.  Might be I64 or F64 bits.
  Bool is_f;
  return TosExprBin(cc,&is_f,11);
}

I64 LexExpressionI64(CCmpCtrl *cc)
{//Eval cur expression, forcing to I64.
  Bool is_f;
  I64 res=TosExprBin(cc,&is_f,11);
  if (is_f)
    res=ToI64(res(F64));
  return res;
}

F64 LexExpressionF64(CCmpCtrl *cc)
{//Eval cur expression, forcing to F64.
  Bool is_f;
  I64 res=TosExprBin(cc,&is_f,11);
  return TosAsF64(res,is_f);
}

//ExePrint/ExePutS for the native port.  TempleOS compiles the text; PopUp and
//friends only ever send calls like "DocMenu(0x1234,0x0);WinGrid(OFF);", so this
//runs a sequence of calls through the translator's function table.
//shortcut: no declarations, assignments or control flow; those raise TosExeErr.

extern U8 *TosFunFind(U8 *name); //C runtime: TosFun* or NULL
extern I64 TosFunCall(U8 *fun,I64 argc,I64 *argv,I64 skip,I64 m);
extern I64 TosFunAddr(U8 *fun);

I64 TosExeArg(CCmpCtrl *cc,Bool *_f)
{
  I64 res;
  U8 *fun;
  *_f=FALSE;
  if (cc->token==TK_STR) {
    res=AStrNew(cc->cur_str); //not freed: the callee may keep it
    Lex(cc);
    return res;
  }
  if (cc->token=='&') {
    if (Lex(cc)!=TK_IDENT || !(fun=TosFunFind(cc->cur_str)))
      TosExeErr(cc,"Expecting function name at ");
    Lex(cc);
    return TosFunAddr(fun);
  }
  return TosExprBin(cc,_f,11);
}

I64 TosExeStmt(CCmpCtrl *cc)
{
  U8 *fun;
  I64 argc=0,argv[16],skip=0,m=1<<63; //bit 63: arg types known (TOS_TYPED)
  Bool is_f;
  F64 d;
  if (cc->token!=TK_IDENT)
    TosExeErr(cc,"Native port runs only function calls; found at ");
  if (!StrCmp(cc->cur_str,"ToI64")) { //intrinsics, not in the function table: GetI64/GetF64
    Lex(cc);                          //send "ToI64(<typed text>);"
    return LexExpressionI64(cc);
  }
  if (!StrCmp(cc->cur_str,"ToF64")) {
    Lex(cc);
    d=LexExpressionF64(cc);
    return d(I64);
  }
  if (!(fun=TosFunFind(cc->cur_str)))
    TosExeErr(cc,"Unknown function at ");
  if (Lex(cc)=='(') {
    Lex(cc);
    while (cc->token!=')') {
      if (argc>=16)
	TosExeErr(cc,"Too many args at ");
      if (cc->token==',') {
	skip|=1<<argc;
	argv[argc++]=0;
      } else {
	argv[argc]=TosExeArg(cc,&is_f);
	if (is_f) m|=1<<argc;
	argc++;
	if (cc->token==')') break;
	if (cc->token!=',')
	  TosExeErr(cc,"Expecting ',' at ");
      }
      if (Lex(cc)==')') { //"F(a,)": the last arg is left empty
	skip|=1<<argc;
	argv[argc++]=0;
      }
    }
    Lex(cc); //Skip ')'
  }
  return TosFunCall(fun,argc,argv,skip,m);
}

I64 TosExeStr(U8 *src)
{
  I64 res=0;
  CCmpCtrl *cc=CmpCtrlNew(src,CCF_DONT_FREE_BUF);
  Lex(cc);
  while (cc->token) {
    if (cc->token==';')
      Lex(cc);
    else
      res=TosExeStmt(cc);
  }
  CmpCtrlDel(cc);
  return res;
}

I64 ExePutS(U8 *buf,U8 *filename=NULL,I64 ccf_flags=0,
	CLexHashTableContext *htc=NULL)
{
  no_warn filename,ccf_flags,htc;
  return TosExeStr(buf);
}

I64 ExePrint(U8 *fmt,...)
{
  U8 *buf=StrPrintJoin(NULL,fmt,argc,argv);
  I64 res=TosExeStr(buf);
  Free(buf);
  return res;
}

I64 ExePrint2(U8 *fmt,...)
{//TempleOS's runs in the current task's compiler context; same thing here
  U8 *buf=StrPrintJoin(NULL,fmt,argc,argv);
  I64 res=TosExeStr(buf);
  Free(buf);
  return res;
}
