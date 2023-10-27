/*-------------------------------------------------------------------------
  main.c - mcs51 specific general functions

  Copyright (C) 1998, Sandeep Dutta . sandeep.dutta@usa.net
  Copyright (C) 1999, Jean-Louis VERN.jlvern@writeme.com
  Copyright (C) 2000, Michael Hope

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
-------------------------------------------------------------------*/
/*
    Note that mlh prepended _mcs51_ on the static functions.  Makes
    it easier to set a breakpoint using the debugger.
*/
#include "common.h"
#include "main.h"
#include "ralloc.h"
#include "gen.h"
#include "peep.h"
#include "rtrack.h"
#include "dbuf_string.h"
#include "../SDCCutil.h"

static char _defaultRules[] =
{
#include "peeph.rul"
};

#define OPTION_SMALL_MODEL          "--model-small"
#define OPTION_MEDIUM_MODEL         "--model-medium"
#define OPTION_LARGE_MODEL          "--model-large"
#define OPTION_HUGE_MODEL           "--model-huge"
#define OPTION_STACK_SIZE       "--stack-size"
#define OPTION_DUAL_DPTR            "--dual-dptr"

static OPTION _mcs51_options[] =
  {
    { 0, OPTION_SMALL_MODEL, NULL, "internal data space is used (default)"},
    { 0, OPTION_MEDIUM_MODEL, NULL, "external paged data space is used"},
    { 0, OPTION_LARGE_MODEL, NULL, "external data space is used"},
    { 0, OPTION_HUGE_MODEL, NULL, "functions are banked, data in external space"},
    { 0, OPTION_STACK_SIZE,  &options.stack_size, "Tells the linker to allocate this space for stack", CLAT_INTEGER },
    { 0, OPTION_DUAL_DPTR, &options.dual_dptr, "Enable support for dual DPTR" },
    { 0, "--parms-in-bank1", &options.parms_in_bank1, "use Bank1 for parameter passing"},
    { 0, "--acall-ajmp",     &options.acall_ajmp, "Use acall/ajmp instead of lcall/ljmp" },
    { 0, "--no-ret-without-call", &options.no_ret_without_call, "Do not use ret independent of acall/lcall" },
    { 0, NULL }
  };

/* list of key words used by msc51 */
static char *_mcs51_keywords[] =
{
  "at",
  "banked",
  "bit",
  "code",
  "critical",
  "data",
  "far",
  "generic",
  "idata",
  "interrupt",
  "naked",
  "near",
  "nonbanked",
  "overlay",
  "pdata",
  "reentrant",
  "sbit",
  "sfr",
  "sfr16",
  "sfr32",
  "using",
  "xdata",
  NULL
};



void mcs51_assignRegisters (ebbIndex *);

static int regParmFlg = 0;      /* determine if we can register a parameter     */
static int regBitParmFlg = 0;   /* determine if we can register a bit parameter */
static struct sym_link *regParmFuncType;

static void
_mcs51_init (void)
{
  asm_addTree (&asm_asxxxx_mapping);
}

static void
_mcs51_reset_regparm (struct sym_link *funcType)
{
  regParmFlg = 0;
  regBitParmFlg = 0;
  regParmFuncType = funcType;
}

static int
_mcs51_regparm (sym_link *l, bool reentrant)
{
  if (IFFUNC_HASVARARGS (regParmFuncType))
    return 0;

  if (IS_STRUCT (l))
    return 0;

  // For struct return keep regs free for pushing hidden parameter.
  if (IS_STRUCT(regParmFuncType->next))
    return 0;

  if (IS_SPEC(l) && (SPEC_NOUN(l) == V_BIT))
    {
      /* bit parameters go to b0 thru b7 */
      if (reentrant && (regBitParmFlg < 8))
        {
          regBitParmFlg++;
          return 12 + regBitParmFlg;
        }
      return 0;
    }
  if (options.parms_in_bank1 == 0)
    {
      /* simple can pass only the first parameter in a register */
      if (regParmFlg)
        return 0;

      regParmFlg = 1;
      return 1;
    }
  else
    {
      int size = getSize(l);
      int remain;

      /* first one goes the usual way to DPTR */
      if (regParmFlg == 0)
        {
          regParmFlg += 4 ;
          return 1;
        }
      /* second one onwards goes to RB1_0 thru RB1_7 */
      remain = regParmFlg - 4;
      if (size > (8 - remain))
        {
          regParmFlg = 12 ;
          return 0;
        }
      regParmFlg += size ;
      return regParmFlg - size + 1;
    }
}

static bool
_mcs51_parseOptions (int *pargc, char **argv, int *i)
{
  /* TODO: allow port-specific command line options to specify
   * segment names here.
   */

  if (strcmp (argv[*i], OPTION_DUAL_DPTR) == 0)
    {
      options.dual_dptr = true;
      return true;
    }

  return FALSE;
}

static void
_mcs51_finaliseOptions (void)
{
  if (options.noXinitOpt)
    port->genXINIT=0;

  switch (options.model)
    {
    case MODEL_SMALL:
      port->mem.default_local_map = data;
      port->mem.default_globl_map = data;
      port->s.ptr_size = 3;
      break;
    case MODEL_MEDIUM:
      port->mem.default_local_map = pdata;
      port->mem.default_globl_map = pdata;
      port->s.ptr_size = 3;
      break;
    case MODEL_LARGE:
    case MODEL_HUGE:
      port->mem.default_local_map = xdata;
      port->mem.default_globl_map = xdata;
      port->s.ptr_size = 3;
      break;
    default:
      port->mem.default_local_map = data;
      port->mem.default_globl_map = data;
      break;
    }

  if (options.parms_in_bank1)
    addSet(&preArgvSet, Safe_strdup("-DSDCC_PARMS_IN_BANK1"));

  /* mcs51 has an assembly coded float library that's almost always reentrant */
  if (!options.useXstack)
    options.float_rent = 1;

  if (options.omitFramePtr)
    port->stack.reent_overhead = 0;

  /* set up external stack location if not explicitly specified */
  if (!options.xstack_loc)
    options.xstack_loc = options.xdata_loc;
}

static void
_mcs51_setDefaultOptions (void)
{
  options.dual_dptr = false;
}

static const char *
_mcs51_getRegName (const struct reg_info *reg)
{
  if (reg)
    return reg->name;
  return "err";
}

static void
_mcs51_genAssemblerPreamble (FILE * of)
{
  if (options.parms_in_bank1)
    {
      int i;
      for (i=0; i < 8 ; i++ )
        fprintf (of, "\tb1_%d = 0x%x \n", i, 8+i);
    }
}

/* Generate interrupt vector table. */
static int
_mcs51_genIVT (struct dbuf_s * oBuf, symbol ** interrupts, int maxInterrupts)
{
  int i;

  dbuf_printf (oBuf, "\t%cjmp\t__sdcc_gsinit_startup\n", options.acall_ajmp?'a':'l');
  if((options.acall_ajmp)&&(maxInterrupts)) dbuf_printf (oBuf, "\t.ds\t1\n");

  /* now for the other interrupts */
  for (i = 0; i < maxInterrupts; i++)
    {
      if (interrupts[i])
        {
          dbuf_printf (oBuf, "\t%cjmp\t%s\n", options.acall_ajmp?'a':'l', interrupts[i]->rname);
          if ( i != maxInterrupts - 1 )
            dbuf_printf (oBuf, "\t.ds\t%d\n", options.acall_ajmp?6:5);
        }
      else
        {
          dbuf_printf (oBuf, "\treti\n");
          if ( i != maxInterrupts - 1 )
            dbuf_printf (oBuf, "\t.ds\t7\n");
        }
    }
  return TRUE;
}

static void
_mcs51_genExtraAreas(FILE *of, bool hasMain)
{
  tfprintf (of, "\t!area\n", HOME_NAME);
  tfprintf (of, "\t!area\n", "GSINIT0 (CODE)");
  tfprintf (of, "\t!area\n", "GSINIT1 (CODE)");
  tfprintf (of, "\t!area\n", "GSINIT2 (CODE)");
  tfprintf (of, "\t!area\n", "GSINIT3 (CODE)");
  tfprintf (of, "\t!area\n", "GSINIT4 (CODE)");
  tfprintf (of, "\t!area\n", "GSINIT5 (CODE)");
  tfprintf (of, "\t!area\n", STATIC_NAME);
  tfprintf (of, "\t!area\n", port->mem.post_static_name);
  tfprintf (of, "\t!area\n", CODE_NAME);
}

static void
_mcs51_genInitStartup (FILE *of)
{
  tfprintf (of, "\t!global\n", "__sdcc_gsinit_startup");
  tfprintf (of, "\t!global\n", "__sdcc_program_startup");
  tfprintf (of, "\t!global\n", "__start__stack");

  if (options.useXstack)
    {
      tfprintf (of, "\t!global\n", "__sdcc_init_xstack");
      tfprintf (of, "\t!global\n", "__start__xstack");
    }

  // if the port can copy the XINIT segment to XISEG
  if (port->genXINIT)
    {
      port->genXINIT(of);
    }

  if (!getenv("SDCC_NOGENRAMCLEAR"))
    tfprintf (of, "\t!global\n", "__mcs51_genRAMCLEAR");
}


/* Generate code to copy XINIT to XISEG */
static void _mcs51_genXINIT (FILE * of)
{
  tfprintf (of, "\t!global\n", "__mcs51_genXINIT");

  if (!getenv("SDCC_NOGENRAMCLEAR"))
    tfprintf (of, "\t!global\n", "__mcs51_genXRAMCLEAR");
}


/* Do CSE estimation */
static bool cseCostEstimation (iCode *ic, iCode *pdic)
{
  operand *result = IC_RESULT(ic);
  sym_link *result_type = operandType(result);

  /* if it is a pointer then return ok for now */
  if (IC_RESULT(ic) && IS_PTR(result_type)) return 1;

  /* if bitwise | add & subtract then no since mcs51 is pretty good at it
     so we will cse only if they are local (i.e. both ic & pdic belong to
     the same basic block */
  if (IS_BITWISE_OP(ic) || ic->op == '+' || ic->op == '-')
    {
      /* then if they are the same Basic block then ok */
      if (ic->eBBlockNum == pdic->eBBlockNum) return 1;
      else return 0;
    }

  /* for others it is cheaper to do the cse */
  return 1;
}

/* Indicate which extended bit operations this port supports */
static bool
hasExtBitOp (int op, int size)
{
  if (op == RRC
      || op == RLC
      || op == GETABIT
      || op == GETBYTE
      || op == GETWORD
      || (op == SWAP && size <= 2)
     )
    return TRUE;
  else
    return FALSE;
}

/* Indicate the expense of an access to an output storage class */
static int
oclsExpense (struct memmap *oclass)
{
  if (IN_FARSPACE(oclass))
    return 1;

  return 0;
}

static bool
_hasNativeMulFor (iCode *ic, sym_link *left, sym_link *right)
{
  if (IS_BITINT (OP_SYM_TYPE (IC_RESULT(ic))) && SPEC_BITINTWIDTH (OP_SYM_TYPE (IC_RESULT(ic))) % 8)
    return false;

  return getSize (left) == 1 && getSize (right) == 1;
}

static int
instructionSize(char *inst, char *op1, char *op2)
{
  #define ISINST(s) (strncmp(inst, (s), sizeof(s)-1) == 0)
  #define IS_A(s) (*(s) == 'a' && *(s+1) == '\0')
  #define IS_C(s) (*(s) == 'c' && *(s+1) == '\0')
  #define IS_Rn(s) (*(s) == 'r' && *(s+1) >= '0' && *(s+1) <= '7')
  #define IS_atRi(s) (*(s) == '@' && *(s+1) == 'r')

  /* Based on the current (2003-08-22) code generation for the
     small library, the top instruction probability is:

       57% mov/movx/movc
        6% push
        6% pop
        4% inc
        4% lcall
        4% add
        3% clr
        2% subb
  */
  /* mov, push, & pop are the 69% of the cases. Check them first! */
  if (ISINST ("mov"))
    {
      if (*(inst+3)=='x') return 1; /* movx */
      if (*(inst+3)=='c') return 1; /* movc */
      if (IS_C (op1) || IS_C (op2)) return 2;
      if (IS_A (op1))
        {
          if (IS_Rn (op2) || IS_atRi (op2)) return 1;
          return 2;
        }
      if (IS_Rn(op1) || IS_atRi(op1))
        {
          if (IS_A(op2)) return 1;
          return 2;
        }
      if (strcmp (op1, "dptr") == 0) return 3;
      if (IS_A (op2) || IS_Rn (op2) || IS_atRi (op2)) return 2;
      return 3;
    }

  if (ISINST ("push")) return 2;
  if (ISINST ("pop")) return 2;

  if (ISINST ("lcall")) return 3;
  if (ISINST ("ret")) return 1;
  if (ISINST ("ljmp")) return 3;
  if (ISINST ("sjmp")) return 2;
  if (ISINST ("rlc")) return 1;
  if (ISINST ("rrc")) return 1;
  if (ISINST ("rl")) return 1;
  if (ISINST ("rr")) return 1;
  if (ISINST ("swap")) return 1;
  if (ISINST ("jc")) return 2;
  if (ISINST ("jnc")) return 2;
  if (ISINST ("jb")) return 3;
  if (ISINST ("jnb")) return 3;
  if (ISINST ("jbc")) return 3;
  if (ISINST ("jmp")) return 1; // always jmp @a+dptr
  if (ISINST ("jz")) return 2;
  if (ISINST ("jnz")) return 2;
  if (ISINST ("cjne")) return 3;
  if (ISINST ("mul")) return 1;
  if (ISINST ("div")) return 1;
  if (ISINST ("da")) return 1;
  if (ISINST ("xchd")) return 1;
  if (ISINST ("reti")) return 1;
  if (ISINST ("nop")) return 1;
  if (ISINST ("acall")) return 2;
  if (ISINST ("ajmp")) return 2;


  if (ISINST ("add") || ISINST ("addc") || ISINST ("subb") || ISINST ("xch"))
    {
      if (IS_Rn(op2) || IS_atRi(op2)) return 1;
      return 2;
    }
  if (ISINST ("inc") || ISINST ("dec"))
    {
      if (IS_A(op1) || IS_Rn(op1) || IS_atRi(op1)) return 1;
      if (strcmp(op1, "dptr") == 0) return 1;
      return 2;
    }
  if (ISINST ("anl") || ISINST ("orl") || ISINST ("xrl"))
    {
      if (IS_C(op1)) return 2;
      if (IS_A(op1))
        {
          if (IS_Rn(op2) || IS_atRi(op2)) return 1;
          return 2;
        }
      else
        {
          if (IS_A(op2)) return 2;
          return 3;
        }
    }
  if (ISINST ("clr") || ISINST ("setb") || ISINST ("cpl"))
    {
      if (IS_A(op1) || IS_C(op1)) return 1;
      return 2;
    }
  if (ISINST ("djnz"))
    {
      if (IS_Rn(op1)) return 2;
      return 3;
    }

  /* If the instruction is unrecognized, we shouldn't try to optimize. */
  /* Return a large value to discourage optimization.                  */
  return 999;
}

static asmLineNode *
newAsmLineNode (void)
{
  asmLineNode *aln;

  aln = Safe_alloc ( sizeof (asmLineNode));
  aln->size = 0;
  aln->regsRead = NULL;
  aln->regsWritten = NULL;

  return aln;
}


typedef struct mcs51operanddata
  {
    char name[6];
    int regIdx1;
    int regIdx2;
    int sfrAddr;
  }
mcs51operanddata;

static mcs51operanddata mcs51operandDataTable[] =
  {
    {"a",    A_IDX,   -1, -1},
    {"ab",   A_IDX,   B_IDX, -1},
    {"ac",   CND_IDX, -1, -1},
    {"acc",  A_IDX,   -1, 0xE0},
    {"ar0",  R0_IDX,  -1, -1},
    {"ar1",  R1_IDX,  -1, -1},
    {"ar2",  R2_IDX,  -1, -1},
    {"ar3",  R3_IDX,  -1, -1},
    {"ar4",  R4_IDX,  -1, -1},
    {"ar5",  R5_IDX,  -1, -1},
    {"ar6",  R6_IDX,  -1, -1},
    {"ar7",  R7_IDX,  -1, -1},
    {"b",    B_IDX,   -1, 0xF0},
    {"b0",   B0_IDX,  BITS_IDX, -1},
    {"b1",   B1_IDX,  BITS_IDX, -1},
    {"b2",   B2_IDX,  BITS_IDX, -1},
    {"b3",   B3_IDX,  BITS_IDX, -1},
    {"b4",   B4_IDX,  BITS_IDX, -1},
    {"b5",   B5_IDX,  BITS_IDX, -1},
    {"b6",   B6_IDX,  BITS_IDX, -1},
    {"b7",   B7_IDX,  BITS_IDX, -1},
    {"bits", BITS_IDX, -1, -1},
    {"c",    CND_IDX, -1, -1},
    {"cy",   CND_IDX, -1, -1},
    {"dph",  DPH_IDX, -1, 0x83},
    {"dpl",  DPL_IDX, -1, 0x82},
    {"dptr", DPL_IDX, DPH_IDX, -1},
    {"f0",   CND_IDX, -1, -1},
    {"f1",   CND_IDX, -1, -1},
    {"ov",   CND_IDX, -1, -1},
    {"p",    CND_IDX, -1, -1},
    {"psw",  CND_IDX, -1, 0xD0},
    {"r0",   R0_IDX,  -1, -1},
    {"r1",   R1_IDX,  -1, -1},
    {"r2",   R2_IDX,  -1, -1},
    {"r3",   R3_IDX,  -1, -1},
    {"r4",   R4_IDX,  -1, -1},
    {"r5",   R5_IDX,  -1, -1},
    {"r6",   R6_IDX,  -1, -1},
    {"r7",   R7_IDX,  -1, -1},
  };

static const unsigned int mcs51operandDataTableSize = sizeof(mcs51operandDataTable)/sizeof(mcs51operanddata);

static int
mcs51operandCompare (const void *key, const void *member)
{
  return strcmp((const char *)key, ((mcs51operanddata *)member)->name);
}

static bool
mcs51isSpecialRegister (const char *op1, const char *reg)
{
  for (unsigned int i = 0; i < mcs51operandDataTableSize; ++i)
    if (mcs51operandDataTable[i].sfrAddr >= 0
        && !strcmp (reg, mcs51operandDataTable[i].name))
      {
        /* either the op1 is the direct known standard name or it could have
           been setup as an alias with known standard address but different name.  */
        if (!strcmp (reg, op1))
          return true;

        if (findSFRSymbolAddress (op1) == mcs51operandDataTable[i].sfrAddr)
          return true;
      }

  return false;
}

// #define dbglog_insn_regs(...) do { __VA_ARGS__; } while (0)
#ifndef dbglog_insn_regs
  #define dbglog_insn_regs(...) do { } while (0)
#endif

static const mcs51operanddata*
updateOpRW (asmLineNode *aln, const char *op_in, const char *optype)
{
  const mcs51operanddata *opdat;

  /* Ignore dots or brackets in operand (bit numbes) for operand table search.
     But remember that it's a bit access for special case handling.  */
  char op[32];
  strncpy (op, op_in, 31);
  op[31] = '\0';

  char *bit_sep;
  if (bit_sep = strchr (op, '.'))
    *bit_sep = '\0';
  else if (bit_sep = strchr (op, '['))
    *bit_sep = '\0';

retry_opdat_search:
  opdat = bsearch (op, mcs51operandDataTable, mcs51operandDataTableSize,
                   sizeof(mcs51operanddata), mcs51operandCompare);

  dbglog_insn_regs (printf ("  updateOpRW op: %s  opdat: %p %s \n", op, opdat, opdat ? opdat->name : ""));

  if (opdat == NULL)
  {
    int sfr_addr = findSFRSymbolAddress (op);
    if (sfr_addr >= 0)
      {
        /* Handle some standard known SFRs, which might be accessed by C
           code directly.  Checking the SFR addresses is more robust, as
           the SFR names are usually user defined.  */
        for (unsigned int ii = 0; ii < mcs51operandDataTableSize; ++ii)
          if (sfr_addr == mcs51operandDataTable[ii].sfrAddr)
            {
              opdat = &mcs51operandDataTable[ii];
              break;
            }
      }
    else
      {
        int sfr_bit_addr = findSFRBitSymbolAddress (op);
        if (sfr_bit_addr >= 0)
          {
            /* If it's a user defined SFR bit address, try to match the
               known standard SFR address.  */
            int sfr_byte_addr = (unsigned int)sfr_bit_addr & ~7u;

            for (unsigned int ii = 0; ii < mcs51operandDataTableSize; ++ii)
              if (sfr_byte_addr == mcs51operandDataTable[ii].sfrAddr)
                {
                  opdat = &mcs51operandDataTable[ii];
                  bit_sep = ".";
                  break;
                }
          }
      }
  }

  if (opdat && strchr(optype,'r'))
    {
      if (opdat->regIdx1 >= 0)
        aln->regsRead = bitVectSetBit (aln->regsRead, opdat->regIdx1);
      if (opdat->regIdx2 >= 0)
        aln->regsRead = bitVectSetBit (aln->regsRead, opdat->regIdx2);

      if (!strcmp (op, "bits"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, B0_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B1_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B2_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B3_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B4_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B5_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B6_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B7_IDX);
        }
    }
  if (opdat && strchr(optype,'w'))
    {
      if (opdat->regIdx1 >= 0)
        aln->regsWritten = bitVectSetBit (aln->regsWritten, opdat->regIdx1);
      if (opdat->regIdx2 >= 0)
        aln->regsWritten = bitVectSetBit (aln->regsWritten, opdat->regIdx2);

      if (!strcmp (op, "bits"))
        {
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B0_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B1_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B2_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B3_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B4_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B5_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B6_IDX);
          aln->regsWritten = bitVectSetBit (aln->regsWritten, B7_IDX);
        }

      /* Any bit access always implies a read of the full register.  */
      if (opdat->regIdx1 == A_IDX && bit_sep)
        aln->regsRead = bitVectSetBit (aln->regsRead, A_IDX);

      if (opdat->regIdx1 == B_IDX && bit_sep)
        aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);

      if (opdat->regIdx1 >= B0_IDX && opdat->regIdx1 <= B7_IDX)
        aln->regsRead = bitVectSetBit (aln->regsRead, BITS_IDX);
    }
  if (op[0] == '@')
    {
      if (!strcmp(op, "@r0"))
        aln->regsRead = bitVectSetBit (aln->regsRead, R0_IDX);
      if (!strcmp(op, "@r1"))
        aln->regsRead = bitVectSetBit (aln->regsRead, R1_IDX);
      if (strstr(op, "dptr"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, DPL_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, DPH_IDX);
        }
      if (strstr(op, "a+"))
        aln->regsRead = bitVectSetBit (aln->regsRead, A_IDX);
    }
  if (strstr (op, "(0+") == op)
    {
      /* ISR push/pop for saving/restoring register banks use direct
         addressing in the form "(base + offset)".  Base is always 0.
         Handle at least r0-r7, so that the insns in the function body
         will see that r0-r7 will be overwritten by the pop insns in the end.  */
      const char* begin_offset = op + 3;
      const char* end_offset = strchr (op, ')');
      if (end_offset != NULL)
        {
          unsigned int sz = end_offset - begin_offset;
          if (sz > 32)
            sz = 32;

          char tmp[33];
          strncpy (tmp, begin_offset, sz);
          tmp[sz] = '\0';

          int offset_num = atoi (tmp);
          if ((offset_num > 0 || (offset_num == 0 && tmp[0] == '0'))
              && offset_num <= 7)
            {
              op[0] = 'r';
              op[1] = offset_num + '0';
              op[2] = '\0';
              goto retry_opdat_search;
            }
        }
    }

  return opdat;
}

typedef struct mcs51opcodedata
  {
    char name[6];
    char class[3];
    char pswtype[3];
    char op1type[3];
    char op2type[3];
    int  implicit_rd_idx;
  }
mcs51opcodedata;

static const mcs51opcodedata mcs51opcodeDataTable[] =
  {
    {"acall","j", "",   "",   "", -1},
    {"add",  "",  "w",  "rw", "r", -1},
    {"addc", "",  "rw", "rw", "r", -1},
    {"ajmp", "j", "",   "",   "", -1},
    {"anl",  "",  "",   "rw", "r", -1},
    {"cjne", "j", "w",  "r",  "r", -1},
    {"clr",  "",  "",   "w",  "", -1},
    {"cpl",  "",  "",   "rw", "", -1},
    {"da",   "",  "rw", "rw", "", -1},
    {"dec",  "",  "",   "rw", "", -1},
    {"div",  "",  "w",  "rw", "", -1},
    {"djnz", "j", "",  "rw",  "", -1},
    {"inc",  "",  "",   "rw", "", -1},
    {"jb",   "j", "",   "r",  "", -1},
    {"jbc",  "j", "",  "rw",  "", -1},
    {"jc",   "j", "r",   "",   "", -1},
    {"jmp",  "j", "",  "",    "", -1},
    {"jnb",  "j", "",   "r",  "", -1},
    {"jnc",  "j", "r",   "",   "", -1},
    {"jnz",  "j", "",  "",    "", A_IDX},
    {"jz",   "j", "",  "",    "", A_IDX},
    {"lcall","j", "",   "",   "", -1},
    {"ljmp", "j", "",   "",   "", -1},
    {"mov",  "",  "",   "w",  "r", -1},
    {"movc", "",  "",   "w",  "r", -1},
    {"movx", "",  "",   "w",  "r", -1},
    {"mul",  "",  "w",  "rw", "", -1},
    {"nop",  "",  "",   "",   "", -1},
    {"orl",  "",  "",   "rw", "r", -1},
    {"pop",  "",  "",   "w",  "", -1},
    {"push", "",  "",   "r",  "", -1},
    {"ret",  "j", "",   "",   "", -1},
    {"reti", "j", "",   "",   "", -1},
    {"rl",   "",  "",   "rw", "", -1},
    {"rlc",  "",  "rw", "rw", "", -1},
    {"rr",   "",  "",   "rw", "", -1},
    {"rrc",  "",  "rw", "rw", "", -1},
    {"setb", "",  "",   "w",  "", -1},
    {"sjmp", "j", "",   "",   "", -1},
    {"subb", "",  "rw", "rw", "r", -1},
    {"swap", "",  "",   "rw", "", -1},
    {"xch",  "",  "",   "rw", "rw", -1},
    {"xchd", "",  "",   "rw", "rw", -1},
    {"xrl",  "",  "",   "rw", "r", -1},
  };

static int
mcs51opcodeCompare (const void *key, const void *member)
{
  return strcmp((const char *)key, ((mcs51opcodedata *)member)->name);
}

static const char* skip_spaces (const char* p)
{
  while (*p && isspace(*p)) p++;
  return p;
}

static asmLineNode *
asmLineNodeFromLineNode (lineNode *ln)
{
  asmLineNode *aln = newAsmLineNode();
  char *op, op1[256], op2[256];
  int opsize;
  const char *p;
  char inst[8];
  mcs51opcodedata *opdat;
  bool op_ignore_case;

  p = ln->line;

  /* extract instruction */

  p = skip_spaces (p);
  for (op = inst, opsize=1; *p; p++)
    {
      if (isspace(*p) || *p == ';' || *p == ':' || *p == '=')
        break;
      else
        if (opsize < sizeof(inst))
          *op++ = tolower(*p), opsize++;
    }
  *op = '\0';

  if (*p == ';' || *p == ':' || *p == '=')
    return aln;

  p = skip_spaces (p);
  if (*p == '=')
    return aln;


  /* extract first operand.  if it starts with '_' that usually means
     it's a case sensitive symbol from c code.  */
  op_ignore_case = *p != '_';

  for (op = op1, opsize=1; *p && *p != ','; p++)
    {
      if (!isspace(*p) && opsize < sizeof(op1))
        *op++ = (op_ignore_case ? tolower(*p) : *p), opsize++;
    }
  *op = '\0';

  if (*p == ',') p++;

  /* extract second operand.  if it starts with '_' that usually means
     it's a case sensitive symbol from c code.  */
  p = skip_spaces (p);
  op_ignore_case = *p != '_';

  for (op = op2, opsize=1; *p && *p != ','; p++)
    {
      if (!isspace(*p) && opsize < sizeof(op2))
        *op++ = (op_ignore_case ? tolower(*p) : *p), opsize++;
    }
  *op = '\0';

  aln->size = instructionSize(inst, op1, op2);

  aln->regsRead = newBitVect (END_IDX);
  aln->regsWritten = newBitVect (END_IDX);

  opdat = bsearch (inst, mcs51opcodeDataTable,
                   sizeof(mcs51opcodeDataTable)/sizeof(mcs51opcodedata),
                   sizeof(mcs51opcodedata), mcs51opcodeCompare);

  if (opdat)
    {
      const mcs51operanddata* op1dat = updateOpRW (aln, op1, opdat->op1type);
      const mcs51operanddata* op2dat = updateOpRW (aln, op2, opdat->op2type);
      if (strchr(opdat->pswtype,'r'))
        aln->regsRead = bitVectSetBit (aln->regsRead, CND_IDX);
      if (strchr(opdat->pswtype,'w'))
        aln->regsWritten = bitVectSetBit (aln->regsWritten, CND_IDX);
      if (opdat->implicit_rd_idx >= 0)
        aln->regsRead = bitVectSetBit (aln->regsRead, opdat->implicit_rd_idx);

      /* special case: 'subb a,acc'
         ignore the ineffective read on 'a'  */
      if (!strcmp (opdat->name, "subb")
          && op1dat && op1dat->regIdx1 == A_IDX
          && op2dat && op2dat->regIdx1 == A_IDX)
        bitVectUnSetBit (aln->regsRead, A_IDX);
    }

  if (!strcmp (inst, "lcall") || !strcmp (inst, "ljmp")
      || !strcmp (inst, "acall") || !strcmp (inst, "ajmp"))
    {
      /* Handle special library calls which do not go through the
         regular function call expansion.  */
      if (!strcmp (op1, "__gptrget")
          || !strcmp (op1, "_gptrgetc")
          || !strcmp (op1, "___gptr_cmp"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, DPH_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, DPL_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
        }
      else if (!strcmp (op1, "__gptrput"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, DPH_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, DPL_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, A_IDX);
        }
      else if (!strcmp (op1, "__decdptr"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, DPH_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, DPL_IDX);
        }
      else if (!strcmp (op1, "___sdcc_xpush_regs_r0"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, A_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R0_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R1_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R2_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R3_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R4_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R5_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R6_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R7_IDX);
        }
      else if (!strcmp (op1, "___sdcc_xpush_regs"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, A_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R1_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R2_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R3_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R4_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R5_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R6_IDX);
          aln->regsRead = bitVectSetBit (aln->regsRead, R7_IDX);
        }
      else if (!strcmp (op1, "___sdcc_xpop_regs_r0"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
        }
      else if (!strcmp (op1, "___sdcc_xpop_regs"))
        {
          aln->regsRead = bitVectSetBit (aln->regsRead, B_IDX);
        }
    }

  /* Function calls and returns insns don't have any explicit operands,
     but rather implicit register operands for arguments/return values.
     Those registers are marked as call-used by genSend, genRet, etc.

     N.B. when the call/return register related insn are emitted,
     they will all have the same iCode set.  Thus need to check here again
     to only use the actual call/jump/return insn.  */
  if (ln->ic
      && (!strcmp (inst, "lcall") || !strcmp (inst, "acall")
          || !strcmp (inst, "ret") || !strcmp (inst, "reti")
          || ((!strcmp (inst, "ljmp") || !strcmp (inst, "sjmp") || !strcmp (inst, "ajmp"))
              && (ln->ic->tailcall || !strcmp (op1, "__sdcc_banked_ret")))))
    {
      if (ln->ic->rCallUsed)
        aln->regsRead = bitVectUnion (aln->regsRead, ln->ic->rCallUsed);
    }

  dbglog_insn_regs ({
    printf ("asmLineNodeFromLineNode ln: %p ic: %p op: %d  line: %s", ln, ln->ic, ln->ic ? ln->ic->op : -1, ln->line);

    printf ("\n   regs wr:      %p  ", aln->regsWritten);
    bitVectPrint (stdout, aln->regsWritten);

    printf ("\n   regs rd:      %p  ", aln->regsRead);
    bitVectPrint (stdout, aln->regsRead);

    printf ("\n   op1: %s  op2: %s\n", op1, op2);
  });

  return aln;
}

static int
getInstructionSize (lineNode *line)
{
  if (!line->aln)
    line->aln = (asmLineNodeBase *) asmLineNodeFromLineNode (line);

  return line->aln->size;
}

static bitVect *
getRegsRead (lineNode *line)
{
  if (!line->aln)
    line->aln = (asmLineNodeBase *) asmLineNodeFromLineNode (line);

  return line->aln->regsRead;
}

static bitVect *
getRegsWritten (lineNode *line)
{
  if (!line->aln)
    line->aln = (asmLineNodeBase *) asmLineNodeFromLineNode (line);

  return line->aln->regsWritten;
}

static const char * models[] = 
{
  "small",  "small-xstack",  "small-stack-auto",  "small-xstack-auto",
  "medium", "medium-xstack", "medium-stack-auto", "medium-xstack-auto",
  "large",  "large-xstack",  "large-stack-auto",  "large-xstack-auto",
  "huge",   "huge-xstack",   "huge-stack-auto",   "huge-xstack-auto",
};

static const char *
get_model (void)
{
  int index;

  switch (options.model)
    {
    case MODEL_SMALL:
      index = 0;
      break;
    case MODEL_MEDIUM:
      index = 4;
      break;
    case MODEL_LARGE:
      index = 8;
      break;
    case MODEL_HUGE:
      index = 12;
      break;
    default:
      werror (W_UNKNOWN_MODEL, __FILE__, __LINE__);
      return "unknown";
    }
  if (options.stackAuto)
    index += 2;
  if (options.useXstack)
    index += 1;
  return models[index];
}

/** $1 is always the basename.
    $2 is always the output file.
    $3 varies
    $l is the list of extra options that should be there somewhere...
    MUST be terminated with a NULL.
*/
static const char *_linkCmd[] =
{
  "sdld", "-nf", "$1", NULL
};

/* $3 is replaced by assembler.debug_opts resp. port->assembler.plain_opts */
static const char *_asmCmd[] =
{
  "sdas8051", "$l", "$3", "$2", "$1.asm", NULL
};

static const char * const _libs[] = { "mcs51", STD_LIB, STD_INT_LIB, STD_LONG_LIB, STD_FP_LIB, NULL, };

/* Globals */
PORT mcs51_port =
{
  TARGET_ID_MCS51,
  "mcs51",
  "MCU 8051",                   /* Target name */
  NULL,                         /* Processor name */
  {
    glue,
    TRUE,                       /* glue_up_main: Emit glue around main */
    MODEL_SMALL | MODEL_MEDIUM | MODEL_LARGE | MODEL_HUGE,
    MODEL_SMALL,
    get_model,
  },
  {                             /* Assembler */
    _asmCmd,
    NULL,
    "-plosgffwy",               /* Options with debug */
    "-plosgffw",                /* Options without debug */
    0,
    ".asm",
    NULL                        /* no do_assemble function */
  },
  {                             /* Linker */
    _linkCmd,
    NULL,
    NULL,
    ".rel",
    1,
    NULL,                       /* crt */
    _libs,                      /* libs */
  },
  {                             /* Peephole optimizer */
    _defaultRules,
    getInstructionSize,
    getRegsRead,
    getRegsWritten,
    mcs51DeadMove,
    mcs51notUsed,
    NULL,
    mcs51notUsedFrom,
    NULL,
    NULL,
    NULL,
    mcs51isSpecialRegister,
  },
  /* Sizes: char, short, int, long, long long, near ptr, far ptr, gptr, func ptr, banked func ptr, bit, float, _BitInt (in bits) */
  { 1, 2, 2, 4, 8, 1, 2, 3, 2, 3, 1, 4, 64 },
  /* tags for generic pointers */
  { 0x00, 0x40, 0x60, 0x80 },   /* far, near, xstack, code */
  {
    "XSTK    (PAG,XDATA)",      // xstack_name
    "STACK   (DATA)",           // istack_name
    "CSEG    (CODE)",           // code_name
    "DSEG    (DATA)",           // data_name
    "ISEG    (DATA)",           // idata_name
    "PSEG    (PAG,XDATA)",      // pdata_name
    "XSEG    (XDATA)",          // xdata_name
    "BSEG    (BIT)",            // bit_name
    "RSEG    (ABS,DATA)",       // reg_name
    "GSINIT  (CODE)",           // static_name
    "OSEG    (OVR,DATA)",       // overlay_name
    "GSFINAL (CODE)",           // post_static_name
    "HOME    (CODE)",           // home_name
    "XISEG   (XDATA)",          // xidata_name - initialized xdata
    "XINIT   (CODE)",           // xinit_name - a code copy of xiseg
    "CONST   (CODE)",           // const_name - const data (code or not)
    "CABS    (ABS,CODE)",       // cabs_name - const absolute data (code or not)
    "XABS    (ABS,XDATA)",      // xabs_name - absolute xdata/pdata
    "IABS    (ABS,DATA)",       // iabs_name - absolute idata/data
    NULL,                       // name of segment for initialized variables
    NULL,                       // name of segment for copies of initialized variables in code space
    NULL,
    NULL,
    1,
    1                           // No fancy alignments supported.
  },
  { _mcs51_genExtraAreas, NULL },
  0,                            // ABI revision
  {
    +1,         /* direction (+1 = stack grows up) */
    0,          /* bank_overhead (switch between register banks) */
    4,          /* isr_overhead */
    1,          /* call_overhead (2 for return address - 1 for pre-incrementing push */
    1,          /* reent_overhead */
    1,          /* banked_overhead (switch between code banks) */
    0           /* sp points directly at last item pushed */
  },
  { -1, FALSE },
  { mcs51_emitDebuggerSymbol },
  {
    256,        /* maxCount */
    2,          /* sizeofElement */
    {6,9,15},   /* sizeofMatchJump[] */
    {9,18,36},  /* sizeofRangeCompare[] */
    4,          /* sizeofSubtract */
    6,          /* sizeofDispatch */
  },
  "_",
  _mcs51_init,
  _mcs51_parseOptions,
  _mcs51_options,
  NULL,
  _mcs51_finaliseOptions,
  _mcs51_setDefaultOptions,
  mcs51_assignRegisters,
  _mcs51_getRegName,
  0,
  _mcs51_rtrackUpdate,
  _mcs51_keywords,
  _mcs51_genAssemblerPreamble,
  NULL,                         /* no genAssemblerEnd */
  _mcs51_genIVT,
  _mcs51_genXINIT,
  _mcs51_genInitStartup,
  _mcs51_reset_regparm,
  _mcs51_regparm,
  NULL,                         /* process_pragma */
  NULL,                         /* getMangledFunctionName */
  _hasNativeMulFor,             /* hasNativeMulFor */
  hasExtBitOp,                  /* hasExtBitOp */
  oclsExpense,                  /* oclsExpense */
  FALSE,                        /* use_dw_for_init */
  TRUE,                         /* little_endian */
  0,                            /* leave lt */
  0,                            /* leave gt */
  1,                            /* transform <= to ! > */
  1,                            /* transform >= to ! < */
  1,                            /* transform != to !(a == b) */
  0,                            /* leave == */
  FALSE,                        /* No array initializer support. */
  cseCostEstimation,
  NULL,                         /* no builtin functions */
  GPOINTER,                     /* treat unqualified pointers as "generic" pointers */
  1,                            /* reset labelKey to 1 */
  1,                            /* globals & local statics allowed */
  0,                            /* Number of registers handled in the tree-decomposition-based register allocator in SDCCralloc.hpp */
  PORT_MAGIC
};
