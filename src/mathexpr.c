/******************************************************************************
**  MODULE:        MATHEXPR.C
**  PROJECT:       EPA SWMM 5.1
**  DESCRIPTION:   Evaluates symbolic mathematical expression consisting
**                 of numbers, variable names, math functions & arithmetic
**                 operators.
**  AUTHORS:       L. Rossman, US EPA - NRMRL
**                 F. Shang, University of Cincinnati
**  VERSION:       5.1.008
**  LAST UPDATE:   04/01/15
******************************************************************************/
/*
**   Operand codes:
** 	   1 = (
** 	   2 = )
** 	   3 = +
** 	   4 = - (subtraction)
** 	   5 = *
** 	   6 = /
** 	   7 = number
** 	   8 = user-defined variable
** 	   9 = - (negative)
**	  10 = cos
**	  11 = sin
**	  12 = tan
**	  13 = cot
**	  14 = abs
**	  15 = sgn
**	  16 = sqrt
**	  17 = log
**	  18 = exp
**	  19 = asin
**	  20 = acos
**	  21 = atan
**	  22 = acot
**    23 = sinh
**	  24 = cosh
**	  25 = tanh
**	  26 = coth
**	  27 = log10
**    28 = step (x<=0 ? 0 : 1)
**	  31 = ^
******************************************************************************/
#define _CRT_SECURE_NO_DEPRECATE

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "headers.h"
#include "mathexpr.h"

#define MAX_STACK_SIZE  1024

//  Local declarations
//--------------------
//  Structure for binary tree representation of math expression
struct TreeNode
{
    int    opcode;                // operator code
    int    ivar;                  // variable index
    double fvalue;                // numerical value
    struct TreeNode *left;        // left sub-tree of tokenized formula
    struct TreeNode *right;       // right sub-tree of tokenized formula
};
typedef struct TreeNode ExprTree;

// math function names
const char *MathFunc[] =  {"COS", "SIN", "TAN", "COT", "ABS", "SGN",
                     "SQRT", "LOG", "EXP", "ASIN", "ACOS", "ATAN",
                     "ACOT", "SINH", "COSH", "TANH", "COTH", "LOG10",
                     "STEP", NULL};

// Local functions
//----------------
static int        sametext(const char *, const char *);
static int        isDigit(char);
static int        isLetter(char);
static void       getToken(SWMM_Project *sp);
static int        getMathFunc(SWMM_Project *sp);
static int        getVariable(SWMM_Project *sp);
static int        getOperand(SWMM_Project *sp);
static int        getLex(SWMM_Project *sp);
static double     getNumber(SWMM_Project *sp);
static ExprTree * newNode(SWMM_Project *sp);
static ExprTree * getSingleOp(SWMM_Project *sp, int *);
static ExprTree * getOp(SWMM_Project *sp, int *);
static ExprTree * getTree(SWMM_Project *sp);
static void       traverseTree(ExprTree *, MathExpr **);
static void       deleteTree(ExprTree *);

// Callback functions
static int    (*getVariableIndex) (SWMM_Project *sp, char *); // return index of named variable

//=============================================================================

int  sametext(const char *s1, const char *s2)
/*
**  Purpose:
**    performs case insensitive comparison of two strings.
**
**  Input:
**    s1 = character string
**    s2 = character string.
**  
**  Returns:
**    1 if strings are the same, 0 otherwise.
*/
{
   int i;
   for (i=0; toupper(s1[i]) == toupper(s2[i]); i++)
     if (!s1[i+1] && !s2[i+1]) return(1);
   return(0);
}

//=============================================================================

int isDigit(char c)
{
    if (c >= '1' && c <= '9') return 1;
    if (c == '0') return 1;
    return 0;
}

//=============================================================================

int isLetter(char c)
{
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c == '_') return 1;
    return 0;
}

//=============================================================================

void getToken(SWMM_Project *sp)
{
    char c[] = " ";

    TMathexprShared *mthxpr = &sp->MathexprShared;

    strcpy(mthxpr->Token, "");
    while ( mthxpr->Pos <= mthxpr->Len &&
        ( isLetter(mthxpr->S[mthxpr->Pos]) || isDigit(mthxpr->S[mthxpr->Pos]) ) )
    {
        c[0] = mthxpr->S[mthxpr->Pos];
        strcat(mthxpr->Token, c);
        mthxpr->Pos++;
    }
    mthxpr->Pos--;
}

//=============================================================================

int getMathFunc(SWMM_Project *sp)
{
    int i = 0;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    while (MathFunc[i] != NULL)
    {
        if (sametext(MathFunc[i], mthxpr->Token)) return i+10;
        i++;
    }
    return(0);
}

//=============================================================================

int getVariable(SWMM_Project *sp)
{
    TMathexprShared *mthxpr = &sp->MathexprShared;

    if ( !getVariableIndex ) return 0;
    mthxpr->Ivar = getVariableIndex(sp, mthxpr->Token);
    if (mthxpr->Ivar >= 0) return 8;
    return 0;
}

//=============================================================================

double getNumber(SWMM_Project *sp)
{
    char c[] = " ";
    char sNumber[255];
    int  errflag = 0;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    /* --- get whole number portion of number */
    strcpy(sNumber, "");
    while (mthxpr->Pos < mthxpr->Len && isDigit(mthxpr->S[mthxpr->Pos]))
    {
        c[0] = mthxpr->S[mthxpr->Pos];
        strcat(sNumber, c);
        mthxpr->Pos++;
    }

    /* --- get fractional portion of number */
    if (mthxpr->Pos < mthxpr->Len)
    {
        if (mthxpr->S[mthxpr->Pos] == '.')
        {
            strcat(sNumber, ".");
            mthxpr->Pos++;
            while (mthxpr->Pos < mthxpr->Len && isDigit(mthxpr->S[mthxpr->Pos]))
            {
                c[0] = mthxpr->S[mthxpr->Pos];
                strcat(sNumber, c);  
                mthxpr->Pos++;
            }
        }

        /* --- get exponent */
        if (mthxpr->Pos < mthxpr->Len && (mthxpr->S[mthxpr->Pos] == 'e' ||
                mthxpr->S[mthxpr->Pos] == 'E'))
        {
            strcat(sNumber, "E");  
            mthxpr->Pos++;
            if (mthxpr->Pos >= mthxpr->Len) errflag = 1;
            else
            {
                if (mthxpr->S[mthxpr->Pos] == '-' || mthxpr->S[mthxpr->Pos] == '+')
                {
                    c[0] = mthxpr->S[mthxpr->Pos];
                    strcat(sNumber, c);  
                    mthxpr->Pos++;
                }
                if (mthxpr->Pos >= mthxpr->Len || !isDigit(mthxpr->S[mthxpr->Pos]))
                    errflag = 1;
                else while ( mthxpr->Pos < mthxpr->Len && isDigit(mthxpr->S[mthxpr->Pos]))
                {
                    c[0] = mthxpr->S[mthxpr->Pos];
                    strcat(sNumber, c);  
                    mthxpr->Pos++;
                }
            }
        }
    }
    mthxpr->Pos--;
    if (errflag) return 0;
    else return atof(sNumber);
}

//=============================================================================

int getOperand(SWMM_Project *sp)
{
    int code;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    switch(mthxpr->S[mthxpr->Pos])
    {
      case '(': code = 1;  break;
      case ')': code = 2;  break;
      case '+': code = 3;  break;
      case '-': code = 4;
        if (mthxpr->Pos < mthxpr->Len-1 &&
            isDigit(mthxpr->S[mthxpr->Pos+1]) &&
            (mthxpr->CurLex == 0 || mthxpr->CurLex == 1))
        {
            mthxpr->Pos++;
            mthxpr->Fvalue = -getNumber(sp);
            code = 7;
        }
        break;
      case '*': code = 5;  break;
      case '/': code = 6;  break;
      case '^': code = 31; break;
      default:  code = 0;
    }
    return code;
}

//=============================================================================

int getLex(SWMM_Project *sp)
{
    int n;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    /* --- skip spaces */
    while ( mthxpr->Pos < mthxpr->Len && mthxpr->S[mthxpr->Pos] == ' ' ) mthxpr->Pos++;
    if ( mthxpr->Pos >= mthxpr->Len ) return 0;

    /* --- check for operand */
    n = getOperand(sp);

    /* --- check for function/variable/number */
    if ( n == 0 )
    {
        if ( isLetter(mthxpr->S[mthxpr->Pos]) )
        {
            getToken(sp);
            n = getMathFunc(sp);
            if ( n == 0 ) n = getVariable(sp);
        }
        else if ( isDigit(mthxpr->S[mthxpr->Pos]) )
        {
            n = 7;
            mthxpr->Fvalue = getNumber(sp);
        }
    }
    mthxpr->Pos++;
    mthxpr->PrevLex = mthxpr->CurLex;
    mthxpr->CurLex = n;
    return n;
}

//=============================================================================

ExprTree * newNode(SWMM_Project *sp)
{
    ExprTree *node;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    node = (ExprTree *) malloc(sizeof(ExprTree));
    if (!node) mthxpr->Err = 2;
    else
    {
        node->opcode = 0;
        node->ivar   = -1;
        node->fvalue = 0.;
        node->left   = NULL;
        node->right  = NULL;
    }
    return node;
}

//=============================================================================

ExprTree * getSingleOp(SWMM_Project *sp, int *lex)
{
    int bracket;
    int opcode;
    ExprTree *left;
    ExprTree *right;
    ExprTree *node;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    /* --- open parenthesis, so continue to grow the tree */
    if ( *lex == 1 )
    {
        mthxpr->Bc++;
        left = getTree(sp);
    }

    else
    {
        /* --- Error if not a singleton operand */
        if ( *lex < 7 || *lex == 9 || *lex > 30)
        {
            mthxpr->Err = 1;
            return NULL;
        }

        opcode = *lex;

        /* --- simple number or variable name */
        if ( *lex == 7 || *lex == 8 )
        {
            left = newNode(sp);
            left->opcode = opcode;
            if ( *lex == 7 ) left->fvalue = mthxpr->Fvalue;
            if ( *lex == 8 ) left->ivar = mthxpr->Ivar;
        }

        /* --- function which must have a '(' after it */
        else
        {
            *lex = getLex(sp);
            if ( *lex != 1 )
            {
                mthxpr->Err = 1;
               return NULL;
            }
            mthxpr->Bc++;
            left = newNode(sp);
            left->left = getTree(sp);
            left->opcode = opcode;
        }
    }   
    *lex = getLex(sp);

    /* --- exponentiation */
    while ( *lex == 31 )
    {
        *lex = getLex(sp);
        bracket = 0;
        if ( *lex == 1 )
        {
            bracket = 1;
            *lex = getLex(sp);
        }
        if ( *lex != 7 )
        {
            mthxpr->Err = 1;
            return NULL;
        }
        right = newNode(sp);
        right->opcode = *lex;
        right->fvalue = mthxpr->Fvalue;
        node = newNode(sp);
        node->left = left;
        node->right = right;
        node->opcode = 31;
        left = node;
        if (bracket)
        {
            *lex = getLex(sp);
            if ( *lex != 2 )
            {
                mthxpr->Err = 1;
                return NULL;
            }
        }
        *lex = getLex(sp);
    }
    return left;
}

//=============================================================================

ExprTree * getOp(SWMM_Project *sp, int *lex)
{
    int opcode;
    ExprTree *left;
    ExprTree *right;
    ExprTree *node;
    int neg = 0;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    *lex = getLex(sp);
    if (mthxpr->PrevLex == 0 || mthxpr->PrevLex == 1)
    {
        if ( *lex == 4 )
        {
            neg = 1;
            *lex = getLex(sp);
        }
        else if ( *lex == 3) *lex = getLex(sp);
    }
    left = getSingleOp(sp, lex);
    while ( *lex == 5 || *lex == 6 )
    {
        opcode = *lex;
        *lex = getLex(sp);
        right = getSingleOp(sp, lex);
        node = newNode(sp);
        if (mthxpr->Err) return NULL;
        node->left = left;
        node->right = right;
        node->opcode = opcode;
        left = node;
    }
    if ( neg )
    {
        node = newNode(sp);
        if (mthxpr->Err) return NULL;
        node->left = left;
        node->right = NULL;
        node->opcode = 9;
        left = node;
    }
    return left;
}

//=============================================================================

ExprTree * getTree(SWMM_Project *sp)
{
    int      lex;
    int      opcode;
    ExprTree *left;
    ExprTree *right;
    ExprTree *node;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    left = getOp(sp, &lex);
    for (;;)
    {
        if ( lex == 0 || lex == 2 )
        {
            if ( lex == 2 ) mthxpr->Bc--;
            break;
        }

        if (lex != 3 && lex != 4 )
        {
            mthxpr->Err = 1;
            break;
        }

        opcode = lex;
        right = getOp(sp, &lex);
        node = newNode(sp);
        if (mthxpr->Err) break;
        node->left = left;
        node->right = right;
        node->opcode = opcode;
        left = node;
    } 
    return left;
}

//=============================================================================

void traverseTree(ExprTree *tree, MathExpr **expr)
// Converts binary tree to linked list (postfix format)
{
    MathExpr *node;

    if ( tree == NULL) return;

    traverseTree(tree->left,  expr);
    traverseTree(tree->right, expr);
    node = (MathExpr *) malloc(sizeof(MathExpr));
    node->fvalue = tree->fvalue;
    node->opcode = tree->opcode;
    node->ivar = tree->ivar;
    node->next = NULL;
    node->prev = (*expr);

    if (*expr) (*expr)->next = node;
    (*expr) = node;
}

//=============================================================================

void deleteTree(ExprTree *tree)
{
    if (tree)
    {
        if (tree->left)  deleteTree(tree->left);
        if (tree->right) deleteTree(tree->right);
        free(tree);
    }
}

//=============================================================================

// Turn on "precise" floating point option                                     //(5.1.008)
#pragma float_control(precise, on, push)                                       //(5.1.008)

double mathexpr_eval(SWMM_Project *sp, MathExpr *expr,
        double (*getVariableValue) (SWMM_Project*, int))
//  Mathematica expression evaluation using a stack
{
    
// --- Note: the ExprStack array must be declared locally and not globally
//     since this function can be called recursively.

    double ExprStack[MAX_STACK_SIZE];
    MathExpr *node = expr;
    double r1, r2;
    int stackindex = 0;
    
    ExprStack[0] = 0.0;
    while(node != NULL)
    {
	switch (node->opcode)
	{
	    case 3:  
		r1 = ExprStack[stackindex];
		stackindex--;
		r2 = ExprStack[stackindex];
		ExprStack[stackindex] = r2 + r1;
		break;

        case 4:  
		r1 = ExprStack[stackindex];
		stackindex--;
		r2 = ExprStack[stackindex];
		ExprStack[stackindex] = r2 - r1;
		break;

        case 5:  
		r1 = ExprStack[stackindex];
		stackindex--;
		r2 = ExprStack[stackindex];
		ExprStack[stackindex] = r2 * r1;
		break;

        case 6:  
		r1 = ExprStack[stackindex];
		stackindex--;
		r2 = ExprStack[stackindex];
		ExprStack[stackindex] = r2 / r1;
		break;				

        case 7:  
		stackindex++;
		ExprStack[stackindex] = node->fvalue;
		break;

        case 8:
        if (getVariableValue != NULL)
        {
           r1 = getVariableValue(sp, node->ivar);
        }
        else r1 = 0.0;
		stackindex++;
		ExprStack[stackindex] = r1;
		break;

        case 9: 
		ExprStack[stackindex] = -ExprStack[stackindex];
		break;

        case 10: 
		r1 = ExprStack[stackindex];
		r2 = cos(r1);
		ExprStack[stackindex] = r2;
		break;

        case 11: 
		r1 = ExprStack[stackindex];
		r2 = sin(r1);
		ExprStack[stackindex] = r2;
		break;

        case 12: 
		r1 = ExprStack[stackindex];
		r2 = tan(r1);
		ExprStack[stackindex] = r2;
		break;

        case 13: 
		r1 = ExprStack[stackindex];
		if (r1 == 0.0) r2 = 0.0;
		else r2 = 1.0/tan( r1 );    
		ExprStack[stackindex] = r2;
		break;

        case 14: 
		r1 = ExprStack[stackindex];
		r2 = fabs( r1 );       
		ExprStack[stackindex] = r2;
		break;

        case 15: 
		r1 = ExprStack[stackindex];
		if (r1 < 0.0) r2 = -1.0;
		else if (r1 > 0.0) r2 = 1.0;
		else r2 = 0.0;
		ExprStack[stackindex] = r2;
		break;

        case 16: 
		r1 = ExprStack[stackindex];
		if (r1 < 0.0) r2 = 0.0;
		else r2 = sqrt( r1 );     
		ExprStack[stackindex] = r2;
		break;

        case 17: 
		r1 = ExprStack[stackindex];
		if (r1 <= 0) r2 = 0.0;
		else r2 = log(r1);
		ExprStack[stackindex] = r2;
		break;

        case 18: 
		r1 = ExprStack[stackindex];
		r2 = exp(r1);
		ExprStack[stackindex] = r2;
		break;

        case 19: 
		r1 = ExprStack[stackindex];
		r2 = asin( r1 );
		ExprStack[stackindex] = r2;
		break;

        case 20: 
		r1 = ExprStack[stackindex];
		r2 = acos( r1 );      
		ExprStack[stackindex] = r2;
		break;

        case 21: 
		r1 = ExprStack[stackindex];
		r2 = atan( r1 );      
		ExprStack[stackindex] = r2;
		break;

        case 22: 
		r1 = ExprStack[stackindex];
		r2 = 1.57079632679489661923 - atan(r1);  
		ExprStack[stackindex] = r2;
		break;

        case 23:
		r1 = ExprStack[stackindex];
		r2 = (exp(r1)-exp(-r1))/2.0;
		ExprStack[stackindex] = r2;
		break;

        case 24: 
		r1 = ExprStack[stackindex];
		r2 = (exp(r1)+exp(-r1))/2.0;
		ExprStack[stackindex] = r2;
		break;

        case 25: 
		r1 = ExprStack[stackindex];
		r2 = (exp(r1)-exp(-r1))/(exp(r1)+exp(-r1));
		ExprStack[stackindex] = r2;
		break;

        case 26: 
		r1 = ExprStack[stackindex];
		r2 = (exp(r1)+exp(-r1))/(exp(r1)-exp(-r1));
		ExprStack[stackindex] = r2;
		break;

        case 27: 
		r1 = ExprStack[stackindex];
		if (r1 == 0.0) r2 = 0.0;
		else r2 = log10( r1 );     
		ExprStack[stackindex] = r2;
		break;

        case 28:
 		r1 = ExprStack[stackindex];
		if (r1 <= 0.0) r2 = 0.0;
		else           r2 = 1.0;
		ExprStack[stackindex] = r2;
		break;
               
        case 31: 
		r1 = ExprStack[stackindex];
		r2 = ExprStack[stackindex-1];
		if (r2 <= 0.0) r2 = 0.0;
		else r2 = exp(r1*log(r2));
		ExprStack[stackindex-1] = r2;
		stackindex--;
		break;
        }
        node = node->next;
    }
    r1 = ExprStack[stackindex];

    // Set result to 0 if it is NaN due to an illegal math op                  //(5.1.008)
    if ( r1 != r1 ) r1 = 0.0;                                                  //(5.1.008)

    return r1;
}

// Turn off "precise" floating point option                                    //(5.1.008)
#pragma float_control(pop)                                                     //(5.1.008)

//=============================================================================

void mathexpr_delete(MathExpr *expr)
{
    if (expr) mathexpr_delete(expr->next);
    free(expr);
}

//=============================================================================

MathExpr * mathexpr_create(SWMM_Project *sp, char *formula,
        int (*getVar) (SWMM_Project *sp, char *))
{
    ExprTree *tree;
    MathExpr *expr = NULL;
    MathExpr *result = NULL;

    TMathexprShared *mthxpr = &sp->MathexprShared;

    getVariableIndex = getVar;

    mthxpr->Err = 0;
    mthxpr->PrevLex = 0;
    mthxpr->CurLex = 0;
    mthxpr->S = formula;
    mthxpr->Len = strlen(mthxpr->S);
    mthxpr->Pos = 0;
    mthxpr->Bc = 0;
    tree = getTree(sp);
    if (mthxpr->Bc == 0 && mthxpr->Err == 0)
    {
	    traverseTree(tree, &expr);
	    while (expr)
	    {
            result = expr;
            expr = expr->prev;
        }
    }
    deleteTree(tree);
    return result;
}
