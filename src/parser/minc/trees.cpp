/* RTcmix  - Copyright (C) 2004  The RTcmix Development Team
   See ``AUTHORS'' for a list of contributors. See ``LICENSE'' for
   the license to this software and for a DISCLAIMER OF ALL WARRANTIES.
*/

/* A revised MinC, supporting lists, types and other fun things.
   Based heavily on the classic cmix version by Lars Graf.
   Doug Scott added the '#' and '//' comment parsing.

   John Gibson <johgibso at indiana dot edu>, 1/20/04
*/

/* This file holds the intermediate tree representation. */

#define DEBUG
#define DEBUG_TRACE

#include "Node.h"
#include "handle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

extern "C" {
	void yyset_lineno(int line_number);
	int yyget_lineno(void);
};

/* We maintain a stack of MAXSTACK lists, which we access when forming 
   user lists (i.e., {1, 2, "foo"}) and function argument lists.  Each
   element of this stack is a list, allocated and freed by push_list and
   pop_list.  <list> is an array of MincListElem structures, each having
   a type and a value, which is encoded in a MincValue union.  Nested lists
   and lists of mixed types are possible.
*/
static MincListElem *sMincList;
static int sMincListLen;
static MincListElem *list_stack[MAXSTACK];
static int list_len_stack[MAXSTACK];
static int list_stack_ptr;

static int sArgListLen;		// number of arguments passed to a user-declared function
static int sArgListIndex;	// used to walk passed-in args for user-declared functions

static bool inCalledFunctionArgList = false;
static const char *sCalledFunction;
static int sFunctionCallDepth = 0;	// level of actively-executing function calls

static bool inFunctionCall() { return sFunctionCallDepth > 0; }

static void copy_tree_tree(Node * tpdest, Node *  tpsrc);
static void copy_sym_tree(Node *  tpdest, Symbol *src);
static void copy_tree_sym(Symbol *dest, Node *  tpsrc);
static void copy_tree_listelem(MincListElem *edest, Node *  tpsrc);
static void copy_listelem_tree(Node *  tpdest, MincListElem *esrc);
static void copy_listelem_elem(MincListElem *edest, MincListElem *esrc);
static void print_value(MincValue *v, MincDataType type);	// TODO: MincValue::print()
static void print_symbol(Symbol * s);		// TODO: Symbol::print()

#if defined(DEBUG_TRACE)
class Trace {
public:
	Trace(const char *func) : mFunc(func) {
		rtcmix_print("%s%s -->\n", spaces, mFunc);
		++sTraceDepth;
		for (int n =0; n<sTraceDepth*3; ++n) { spaces[n] = ' '; }
		spaces[sTraceDepth*3] = '\0';
	}
	static char *getBuf() { return sMsgbuf; }
	static void printBuf() { rtcmix_print("%s%s", spaces, sMsgbuf); }
	~Trace() {
		 --sTraceDepth;
		for (int n =0; n<sTraceDepth*3; ++n) { spaces[n] = ' '; }
		spaces[sTraceDepth*3] = '\0';
		rtcmix_print("%s<-- %s\n", spaces, mFunc);
	}
private:
	static char sMsgbuf[];
	const char *mFunc;
	static int sTraceDepth;
	static char spaces[];
};
	
char Trace::sMsgbuf[256];
int Trace::sTraceDepth = 0;
char Trace::spaces[128];

#ifdef __GNUC__
#define ENTER() Trace __trace__(__PRETTY_FUNCTION__)
#else
#define ENTER() Trace __trace__(__FUNCTION__)
#endif
#define TPRINT(...) do { snprintf(Trace::getBuf(), 256, __VA_ARGS__); Trace::printBuf(); } while(0)
#else
#define ENTER()
#define TPRINT(...)
#endif

#undef DEBUG_MEMORY
#ifdef DEBUG_MEMORY
static int numNodes = 0;
#endif

static const char *s_NodeKinds[] = {
   "NodeZero",
   "NodeSeq",
   "NodeStore",
   "NodeList",
   "NodeListElem",
   "NodeEmptyListElem",
   "NodeSubscriptRead",
   "NodeSubscriptWrite",
   "NodeOpAssign",
   "NodeName",
   "NodeAutoName",
   "NodeConstf",
   "NodeString",
   "NodeFuncDef",
   "NodeArgList",
   "NodeArgListElem",
   "NodeRet",
   "NodeFuncSeq",
   "NodeCall",
   "NodeAnd",
   "NodeOr",
   "NodeOperator",
   "NodeUnaryOperator",
   "NodeNot",
   "NodeRelation",
   "NodeIf",
   "NodeWhile",
   "NodeFor",
   "NodeIfElse",
   "NodeDecl",
   "NodeFuncDecl",
   "NodeBlock",
   "NodeNoop"
};

static const char *s_OpKinds[] = {
	"ILLEGAL",
	"ILLEGAL",
	"+",
	"-",
	"*",
	"/",
	"%",
	"^",
	"-",
	"==",
	"!=",
	"<",
	">",
	"<=",
	">="
};

static const char *printNodeKind(NodeKind k)
{
	return s_NodeKinds[k];
}

static const char *printOpKind(OpKind k)
{
	return s_OpKinds[k];
}

/* prototypes for local functions */
static int cmp(MincFloat f1, MincFloat f2);
static void push_list(void);
static void pop_list(void);

/* floating point comparisons:
     f1 < f2   ==> -1
     f1 == f2  ==> 0
     f1 > f2   ==> 1 
*/
static int
cmp(MincFloat f1, MincFloat f2)
{
   if (fabs((double) f1 - (double) f2) < EPSILON) {
      /* printf("cmp=%g %g %g \n",f1,f2,fabs(f1-f2)); */
      return 0;
   }
   if ((f1 - f2) > EPSILON) { 
      /* printf("cmp > %g %g %g \n",f1,f2,fabs(f1-f2)); */
      return 1;
   }
   if ((f2 - f1) > EPSILON) {
      /* printf("cmp <%g %g %g \n",f1,f2,fabs(f1-f2)); */
      return -1;
   }
   return 0;
}

MincList::MincList(int inLen) : len(inLen), refcount(0), data(NULL)
{
	ENTER();
	if (inLen > 0) {
		data = new MincListElem[len];
	}
	TPRINT("MincList::MincList: %p alloc'd at len %d\n", this, inLen);
}

void
MincList::resize(int newLen)
{
	MincListElem *oldList = data;
	data = new MincListElem[newLen];
	int i;
	for (i = 0; i < len; ++i) {
		data[i] = oldList[i];
	}
	for (; i < newLen; i++) {
		data[i] = 0.0;
	}
	len = newLen;
}

/* ========================================================================== */
/* Tree nodes */

Node::Node(OpKind op, NodeKind kind)
	: kind(kind), type(MincVoidType), op(op), lineno(yyget_lineno())
{
	TPRINT("Node::Node (%s) this=%p\n", classname(), this);
	v.list = NULL;
#ifdef DEBUG_MEMORY
	++numNodes;
	TPRINT("[%d nodes in existence]\n", numNodes);
#endif
}

Node::~Node()
{
#ifdef DEBUG_MEMORY
	TPRINT("entering ~Node (%s) this=%p\n", classname(), this);
#endif
	if (this->type == MincHandleType) {
		unref_handle(this->v.handle);
	}
	else if (this->type == MincListType) {
		unref_value_list(&this->v);
	}
	this->type = MincVoidType;		// To prevent double-free
#ifdef DEBUG_MEMORY
	--numNodes;
	TPRINT("[%d nodes left]\n", numNodes);
#endif
}

const char * Node::classname() const
{
	return printNodeKind(kind);
}

void	Node::setType(MincDataType inType)
{
	if (type != MincVoidType) {
		// TODO: IS THIS EVER NOT A WARNING?
	}
	type = inType;
}

Node *	Node::exct()
{
	if (was_rtcmix_error())
		return NULL;
	ENTER();
	TPRINT("%s::exct() this=%p\n", classname(), this);
	if (inFunctionCall() && lineno > 0) {
		yyset_lineno(lineno);
	}
	Node *outNode = doExct();	// this is redefined on all subclasses
	TPRINT("%s done, node=%p, %s\n", classname(), outNode, MincTypeName(type));
	return outNode;
}

NodeNoop::~NodeNoop() {}	// to make sure there is a vtable

/* ========================================================================== */
/* Operators */

/* ---------------------------------------------------------- do_op_string -- */
Node *	NodeOp::do_op_string(const char *str1, const char *str2, OpKind op)
{
	ENTER();
   char *s;
   unsigned long   len;

   switch (op) {
      case OpPlus:   /* concatenate */
         len = (strlen(str1) + strlen(str2)) + 1;
         s = (char *) emalloc(sizeof(char) * len);
         if (s == NULL)
            return NULL;	// TODO: check this
         strcpy(s, str1);
         strcat(s, str2);
         this->v.string = s;
         // printf("str1=%s, str2=%s len=%d, s=%s\n", str1, str2, len, s);
         break;
      case OpMinus:
      case OpMul:
      case OpDiv:
      case OpMod:
      case OpPow:
      case OpNeg:
         minc_warn("unsupported operation on a string");
         return this;		// TODO: check
      default:
         minc_internal_error("invalid string operator");
         break;
   }
   this->type = MincStringType;
	return this;
}


/* ------------------------------------------------------------- do_op_num -- */
Node *	NodeOp::do_op_num(const MincFloat val1, const MincFloat val2, OpKind op)
{
	ENTER();
   switch (op) {
      case OpPlus:
         this->v.number = val1 + val2;
         break;
      case OpMinus:
         this->v.number = val1 - val2;
         break;
      case OpMul:
         this->v.number = val1 * val2;
         break;
      case OpDiv:
         this->v.number = val1 / val2;
         break;
      case OpMod:
         this->v.number = (MincFloat) ((long) val1 % (long) val2);
         break;
      case OpPow:
         this->v.number = pow(val1, val2);
         break;
      case OpNeg:
         this->v.number = -val1;        /* <val2> ignored */
         break;
      default:
         minc_internal_error("invalid numeric operator");
         break;
   }
   this->type = MincFloatType;
	return this;
}


/* ------------------------------------------------------ do_op_handle_num -- */
Node *	NodeOp::do_op_handle_num(const MincHandle val1, const MincFloat val2,
      OpKind op)
{
	ENTER();
   switch (op) {
      case OpPlus:
      case OpMinus:
      case OpMul:
      case OpDiv:
      case OpMod:
      case OpPow:
         this->v.handle = minc_binop_handle_float(val1, val2, op);
         ref_handle(this->v.handle);
         break;
      case OpNeg:
         this->v.handle = minc_binop_handle_float(val1, -1.0, OpMul);	// <val2> ignored
         ref_handle(this->v.handle);
         break;
      default:
         minc_internal_error("invalid operator for handle and number");
         break;
   }
   this->type = MincHandleType;
	return this;
}


/* ------------------------------------------------------ do_op_num_handle -- */
Node *	NodeOp::do_op_num_handle(const MincFloat val1, const MincHandle val2,
      OpKind op)
{
	ENTER();
   switch (op) {
      case OpPlus:
      case OpMinus:
      case OpMul:
      case OpDiv:
      case OpMod:
      case OpPow:
         this->v.handle = minc_binop_float_handle(val1, val2, op);
         ref_handle(this->v.handle);
         break;
      case OpNeg:
         /* fall through */
      default:
         minc_internal_error("invalid operator for handle and number");
         break;
   }
   this->type = MincHandleType;
	return this;
}


/* --------------------------------------------------- do_op_handle_handle -- */
Node *	NodeOp::do_op_handle_handle(const MincHandle val1, const MincHandle val2,
      OpKind op)
{
	ENTER();
	switch (op) {
	case OpPlus:
	case OpMinus:
	case OpMul:
	case OpDiv:
	case OpMod:
	case OpPow:
		this->v.handle = minc_binop_handles(val1, val2, op);
        ref_handle(this->v.handle);
		break;
	case OpNeg:
	default:
		minc_internal_error("invalid binary handle operator");
		break;
	}
	if (this->v.handle)
		this->type = MincHandleType;
	return this;
}


/* ---------------------------------------------------- do_op_list_iterate -- */
/* Iterate over <child>'s list, performing the operation specified by <op>,
   using the scalar <val>, for each list element.  Store the result into a
   new list for <this>, so that child's list is unchanged.
*/
Node *	NodeOp::do_op_list_iterate(Node *child, const MincFloat val, const OpKind op)
{
	ENTER();
   int i;
   MincListElem *dest;
   const MincList *srcList = child->v.list;
   const int len = srcList->len;
   MincListElem *src = srcList->data;
   MincList *destList = new MincList(len);
   dest = destList->data;
   assert(len >= 0);
   switch (op) {
      case OpPlus:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = src[i].val.number + val;
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpMinus:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = src[i].val.number - val;
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpMul:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = src[i].val.number * val;
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpDiv:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = src[i].val.number / val;
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpMod:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = (MincFloat) ((long) src[i].val.number
                                                            % (long) val);
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpPow:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = (MincFloat) pow((double) src[i].val.number,
                                                                  (double) val);
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      case OpNeg:
         for (i = 0; i < len; i++) {
            if (src[i].type == MincFloatType)
               dest[i].val.number = -src[i].val.number;    /* <val> ignored */
            else
               dest[i].val = src[i].val;
            dest[i].type = src[i].type;
         }
         break;
      default:
         for (i = 0; i < len; i++)
            dest[i].val.number = 0.0;
         minc_internal_error("invalid list operator");
         break;
   }
   assert(this->type == MincVoidType);	// are we ever overwriting these?
   this->type = MincListType;
   assert(this->v.list == NULL);
   this->v.list = destList;
   TPRINT("do_op_list_iterate: list %p refcount %d -> %d\n", destList, destList->refcount, destList->refcount+1);
   ++destList->refcount;
	return this;
}

/* ---------------------------------------------------- do_op_list_list -- */
/* Currently just supports + and +=, concatenating the lists.  Store the result into a
 new list for <this>, so that child's list is unchanged.  N.B. This will operate on zero-length
 and NULL lists as well.
 */
Node *	NodeOp::do_op_list_list(Node *child1, Node *child2, const OpKind op)
{
	ENTER();
	int i, n;
	const MincList *list1 = child1->v.list;
	const int len1 = (list1) ? list1->len : 0;
	MincListElem *src1 = (list1) ? list1->data : NULL;
	const MincList *list2 = child2->v.list;
	const int len2 = (list2) ? list2->len : 0;
	MincListElem *src2 = (list2) ? list2->data : NULL;
	
	MincList *destList;
	MincListElem *dest;
	switch (op) {
		case OpPlus:
			destList = new MincList(len1+len2);
			dest = destList->data;
			for (i = 0, n = 0; i < len1; ++i, ++n) {
				dest[i] = src1[n];
			}
			for (n = 0; n < len2; ++i, ++n) {
				dest[i] = src2[n];
			}
			break;
		default:
			minc_warn("invalid operator for two lists");
			destList = new MincList(0);		// return zero-length list
			break;
	}
	if (this->type == MincListType) {	// if we are overwriting
		unref_value_list(&this->v);
	}
	this->type = MincListType;
	this->v.list = destList;
	TPRINT("do_op_list_list: list %p refcount %d -> %d\n", destList, destList->refcount, destList->refcount+1);
	++destList->refcount;
	return this;
}

/* ========================================================================== */
/* Tree execution and disposal */

/* ------------------------------------------------------ check_list_count -- */
/* This protects us against a situation that can arise due to our use of
   '{' and '}' to delimit both statements and list contents.  If you write 
   the following in a script, it will quickly chew through all available
   memory, as it allocates a zero-length block for an empty list on each
   iteration.

      while (1) {}

   This function prevents this from going on for too many thousands of
   iterations.
*/
#define MAX_LISTS 50000

static int
check_list_count()
{
   static int list_count = 0;
   if (++list_count > MAX_LISTS) {
      minc_die("Bailing out due to suspected infinite loop on "
               "empty code block\n(e.g., \"while (1) {}\").");
      return -1;
   }
   return 0;
}


/* ------------------------------------------------------------------ exct -- */
/* These recursive functions interprets the intermediate code.
*/

Node *	NodeConstf::doExct()
{
	type = MincFloatType;
	v.number = u.number;
	return this;
}

Node *	NodeString::doExct()
{
	type = MincStringType;
	v.string = u.string;
	return this;
}

Node *	NodeName::doExct()
{
	/* look up the symbol */
	u.symbol = lookup(_symbolName, AnyLevel);
	return finishExct();
}

Node *	NodeName::finishExct()
{
	if (u.symbol) {
		TPRINT("%s: symbol %p\n", classname(), u.symbol);
		/* For now, symbols for functions cannot be an RHS */
		if (u.symbol->node != NULL) {
			minc_die("Cannot use function '%s' as a variable", symbolName());
		}
		else {
			/* also assign the symbol's value into tree's value field */
			TPRINT("NodeName/NodeAutoName: copying value from symbol '%s' to us\n", u.symbol->name);
			copy_sym_tree(this, u.symbol);
			assert(type == u.symbol->type);
		}
	}
	else {
		// FIXME: install id w/ value of 0, then warn??
		minc_die("'%s' is not declared", symbolName());
//		return NULL;	// FIX ME: return NULL?  Void Node?
	}
	return this;
}

Node *	NodeAutoName::doExct()
{
	/* look up the symbol */
	u.symbol = lookupOrAutodeclare(symbolName(), sFunctionCallDepth > 0 ? YES : NO);
	return finishExct();
}

Node *	NodeListElem::doExct()
{
	TPRINT("NodeListElem exct'ing Node link %p\n", child(0));
	child(0)->exct();
	if (sMincListLen == MAXDISPARGS) {
		minc_die("exceeded maximum number of items for a list");
		return this;	// TODO: handle no-die case
	}
	else {
		TPRINT("NodeListElem %p evaluating payload child Node %p\n", this, child(1));
		Node * tmp = child(1)->exct();
		/* Copy entire MincValue union from expr to this and to stack. */
		TPRINT("NodeListElem %p copying child value into self and stack\n", this);
		copy_tree_tree(this, tmp);
		copy_tree_listelem(&sMincList[sMincListLen], tmp);
		sMincListLen++;
		TPRINT("NodeListElem: list at level %d now len %d\n", list_stack_ptr, sMincListLen);
	}
	return this;
}

Node *	NodeList::doExct()
{
	push_list();
	child(0)->exct();     /* NB: increments sMincListLen */
	MincList *theList;
	if (check_list_count() < 0)
		return this;
	theList = new MincList(sMincListLen);
	if (theList == NULL)
		return NULL;
	if (type == MincListType && v.list != NULL)
		unref_value_list(&this->v);
	type = MincListType;
	v.list = theList;
	TPRINT("MincList %p assigned to self\n", theList);
	theList->refcount = 1;
	TPRINT("MincList refcount = 1\n");
	// Copy from stack list into tree list.
	for (int i = 0; i < sMincListLen; ++i)
		copy_listelem_elem(&theList->data[i], &sMincList[i]);
	pop_list();
	return this;
}

Node *	NodeSubscriptRead::doExct()	// was exct_subscript_read()
{
	ENTER();
	child(0)->exct();         /* lookup target */
	child(1)->exct();
	if (child(1)->type != MincFloatType) {
		minc_die("list index must be a number");
		return this;
	}
	if (child(0)->u.symbol->type == MincListType) {
		MincListElem elem;
		MincFloat fltindex = child(1)->v.number;
		int index = (int) fltindex;
		MincFloat frac = fltindex - index;
		 MincList *theList = child(0)->u.symbol->v.list;
		 if (theList == NULL) {
			 minc_die("attempt to index a NULL list");
			 return this;
		 }
			int len = theList->len;
		 if (len == 0) {
			 minc_die("attempt to index an empty list");
			 return this;
		 }
		 if (fltindex < 0.0) {    /* -1 means last element */
			 if (fltindex <= -2.0)
				 minc_warn("negative index ... returning last element");
			 index = len - 1;
			 fltindex = (MincFloat) index;
		 }
		 else if (fltindex > (MincFloat) (len - 1)) {
			 minc_warn("attempt to index past the end of list ... "
					   "returning last element");
			 index = len - 1;
			 fltindex = (MincFloat) index;
		 }
		 elem.type = MincVoidType;
		 copy_listelem_elem(&elem, &theList->data[index]);
		
		/* do linear interpolation for float items */
		if (elem.type == MincFloatType && frac > 0.0 && index < len - 1) {
			MincListElem elem2 = theList->data[index + 1];
			if (elem2.type == MincFloatType)
				this->v.number = elem.val.number
				+ (frac * (elem2.val.number - elem.val.number));
			else  /* can't interpolate btw. a number and another type */
				this->v.number = elem.val.number;
			this->type = elem.type;
		}
		else {
			copy_listelem_tree(this, &elem);
		}
		clear_elem(&elem);
	}
	else {
		minc_die("attempt to index a variable that's not a list");
	}
	return this;
}

Node *	NodeSubscriptWrite::doExct()	// was exct_subscript_write()
{
	ENTER();
	child(0)->exct();         /* lookup target */
	child(1)->exct();         /* index */
	child(2)->exct();         /* expression to store */
	if (child(1)->type != MincFloatType) {
		minc_die("list index must be a number");
		return this;	// TODO
	}
	if (child(0)->u.symbol->type != MincListType) {
		minc_die("attempt to index a variable that's not a list");
		return this;	// TODO
	}
	int len = 0;
	MincList *theList = child(0)->u.symbol->v.list;
	MincFloat fltindex = child(1)->v.number;
	int index = (int) fltindex;
	if (fltindex - (MincFloat) index > 0.0)
		minc_warn("list index must be integer ... correcting");
	if (theList != NULL) {
		len = theList->len;
		assert(len >= 0);    /* NB: okay to have zero-length list */
	}
	if (index == -1)     /* means last element */
		index = len > 0 ? len - 1 : 0;
	else if (index >= len) {
		/* resize list */
		int newslots;
		newslots = len > 0 ? (index - (len - 1)) : index + 1;
		len += newslots;
		if (len < 0) {
			minc_die("list array subscript exceeds integer size limit!");
		}
		if (theList == NULL)
			child(0)->u.symbol->v.list = theList = new MincList(len);
		else
			theList->resize(len);
		TPRINT("exct_subscript_write: MincList %p expanded to len %d\n",
			   theList->data, len);
		// Ref the list if just allocated.
		if (theList->refcount == 0)
			theList->refcount = 1;
		TPRINT("list %p refcount = 1\n", theList);
	}
	copy_tree_listelem(&theList->data[index], child(2));
	assert(theList->data[index].type == child(2)->type);
	copy_tree_tree(this, child(2));
	return this;
}

Node *	NodeCall::doExct()
{
	push_list();
	Symbol *funcSymbol = lookup(_functionName, GlobalLevel);
	if (funcSymbol) {
		sCalledFunction = _functionName;
		/* The function's definition node was stored on the symbol at declaration time.
		 However, if a function was called on a non-function symbol, the tree will be NULL.
		 */
		Node * funcDef = funcSymbol->node;
		if (funcDef) {
			TPRINT("NodeCall: func def = %p\n", funcDef);
			TPRINT("NodeCall: exp decl list = %p\n", child(0));
			child(0)->exct();	// execute arg expression list
			push_function_stack();
			push_scope();
			int savedLineNo, savedScope, savedCallDepth;
			Node * temp = NULL;
			try {
				/* The exp list is copied to the symbols for the function's arg list. */
				funcDef->child(1)->exct();
				savedLineNo = yyget_lineno();
				savedScope = current_scope();
				++sFunctionCallDepth;
				savedCallDepth = sFunctionCallDepth;
				TPRINT("NodeCall(%p): executing %s() block node %p, call depth now %d\n",
					   this, sCalledFunction, funcDef->child(2), savedCallDepth);
				temp = funcDef->child(2)->exct();
			}
			catch (Node * returned) {	// This catches return statements!
				TPRINT("NodeCall(%p) caught %p return stmt throw - restoring call depth %d\n",
					   this, returned, savedCallDepth);
				temp = returned;
				sFunctionCallDepth = savedCallDepth;
				restore_scope(savedScope);
			}
			catch(...) {	// Anything else is an error
				pop_function_stack();
				--sFunctionCallDepth;
				throw;
			}
			--sFunctionCallDepth;
			TPRINT("NodeCall: function call depth => %d\n", sFunctionCallDepth);
			// restore parser line number
			yyset_lineno(savedLineNo);
			TPRINT("NodeCall copying def exct results into self\n");
			copy_tree_tree(this, temp);
			pop_function_stack();
		}
		else {
			minc_die("'%s' is not a function", funcSymbol->name);
		}
		sCalledFunction = NULL;
	}
	else {
		child(0)->exct();
		MincListElem retval;
		int result = call_builtin_function(_functionName, sMincList, sMincListLen,
										   &retval);
		if (result < 0) {
			result = call_external_function(_functionName, sMincList, sMincListLen,
											&retval);
		}
		copy_listelem_tree(this, &retval);
		assert(this->type == retval.type);
		clear_elem(&retval);
		if (result != 0) {
			set_rtcmix_error(result);	// store fatal error from RTcmix layer (EMBEDDED ONLY)
		}
	}
	pop_list();
	return this;
}

Node *	NodeStore::doExct()
{
#ifdef ORIGINAL_CODE
	/* N.B. Now that symbol lookup is part of tree, this happens in
	 the NodeName stored as child[0] */
	TPRINT("NodeStore(%p): evaluate LHS %p (child 0)\n", this, child(0));
	child(0)->exct();
	/* evaluate RHS expression */
	TPRINT("NodeStore(%p): evaluate RHS (child 1)\n", this);
	child(1)->exct();
#else
	/* evaluate RHS expression */
	TPRINT("NodeStore(%p): evaluate RHS (child 1) FIRST\n", this);
	child(1)->exct();
	/* N.B. Now that symbol lookup is part of tree, this happens in
	 the NodeName stored as child[0] */
	TPRINT("NodeStore(%p): evaluate LHS %p (child 0)\n", this, child(0));
	child(0)->exct();
#endif
	TPRINT("NodeStore(%p): copying value from RHS (%p) to LHS's symbol (%p)\n",
		   this, child(1), child(0)->u.symbol);
	/* Copy entire MincValue union from expr to id sym and to this. */
	copy_tree_sym(child(0)->u.symbol, child(1));
	TPRINT("NodeStore: copying value from RHS (%p) to here (%p)\n", child(1), this);
	copy_tree_tree(this, child(1));
	return this;
}

Node *	NodeOpAssign::doExct()		// was exct_opassign()
{
	ENTER();
	Node *tp0 = child(0)->exct();
	Node *tp1 = child(1)->exct();
	
	if (tp0->u.symbol->type != MincFloatType || tp1->type != MincFloatType) {
		minc_warn("can only use '%c=' with numbers",
				  op == OpPlus ? '+' : (op == OpMinus ? '-'
										: (op == OpMul ? '*' : '/')));
		//FIXME: Is this correct?
		//      memcpy(&this->v, &tp0->u.symbol->v, sizeof(MincValue));
		//      this->type = tp0->type;
		copy_sym_tree(this, tp0->u.symbol);
		return this;
	}
	
	switch (this->op) {
		case OpPlus:
			tp0->u.symbol->v.number += tp1->v.number;
			break;
		case OpMinus:
			tp0->u.symbol->v.number -= tp1->v.number;
			break;
		case OpMul:
			tp0->u.symbol->v.number *= tp1->v.number;
			break;
		case OpDiv:
			tp0->u.symbol->v.number /= tp1->v.number;
			break;
		default:
			minc_internal_error("exct: tried to execute invalid NodeOpAssign");
			break;
	}
	tp0->u.symbol->type = tp1->type;
	this->v.number = tp0->u.symbol->v.number;
	this->type = tp1->type;
	return this;
}

Node *	NodeNot::doExct()
{
	this->type = MincFloatType;
	if (cmp(0.0, child(0)->exct()->v.number) == 0)
		this->v.number = 1.0;
	else
		this->v.number = 0.0;
	return this;
}

Node *	NodeAnd::doExct()
{
	this->type = MincFloatType;
	this->v.number = 0.0;
	if (cmp(0.0, child(0)->exct()->v.number) != 0) {
		if (cmp(0.0, child(1)->exct()->v.number) != 0) {
			this->type = MincFloatType;
			this->v.number = 1.0;
		}
	}
	return this;
}

Node *	NodeRelation::doExct()		// was exct_relation()
{
	ENTER();
	Node *tp0 = child(0)->exct();
	Node *tp1 = child(1)->exct();
	
	this->type = MincFloatType;
	
	if (tp0->type != tp1->type) {
		minc_warn("operator %s: attempt to compare variables having different types", printOpKind(this->op));
		this->v.number = 0.0;
		return this;
	}
	
	switch (this->op) {
		case OpEqual:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) == 0)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				case MincStringType:
					if (strcmp(tp0->v.string, tp1->v.string) == 0)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		case OpNotEqual:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) == 0)
						this->v.number = 0.0;
					else
						this->v.number = 1.0;
					break;
				case MincStringType:
					if (strcmp(tp0->v.string, tp1->v.string) == 0)
						this->v.number = 0.0;
					else
						this->v.number = 1.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		case OpLess:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) == -1)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		case OpGreater:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) == 1)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		case OpLessEqual:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) <= 0)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		case OpGreaterEqual:
			switch (tp0->type) {
				case MincFloatType:
					if (cmp(tp0->v.number, tp1->v.number) >= 0)
						this->v.number = 1.0;
					else
						this->v.number = 0.0;
					break;
				default:
					goto unsupported_type;
					break;
			}
			break;
		default:
			minc_internal_error("exct: tried to execute invalid NodeRelation");
			break;
	}
	return this;
unsupported_type:
	minc_internal_error("operator %s: can't compare this type of object", printOpKind(this->op));
	return this;	// TODO
}

Node *	NodeOp::doExct()
{
	ENTER();
	Node *child0 = child(0)->exct();
	Node *child1 = child(1)->exct();
	switch (child0->type) {
		case MincFloatType:
			switch (child1->type) {
				case MincFloatType:
					do_op_num(child0->v.number, child1->v.number, this->op);
					break;
				case MincStringType:
				{
					char buf[64];
					snprintf(buf, 64, "%g", child0->v.number);
					do_op_string(buf, child1->v.string, this->op);
				}
					break;
				case MincHandleType:
					do_op_num_handle(child0->v.number, child1->v.handle, this->op);
					break;
				case MincListType:
					/* Check for nonsensical ops. */
					if (this->op == OpMinus)
						minc_warn("can't subtract a list from a number");
					else if (this->op == OpDiv)
						minc_warn("can't divide a number by a list");
					else
						do_op_list_iterate(child1, child0->v.number, this->op);
					break;
				default:
					minc_internal_error("operator %s: invalid rhs type: %s", printOpKind(this->op), MincTypeName(child1->type));
					break;
			}
			break;
		case MincStringType:
			switch (child1->type) {
				case MincFloatType:
				{
					char buf[64];
					snprintf(buf, 64, "%g", child1->v.number);
					do_op_string(child0->v.string, buf, this->op);
				}
					break;
				case MincStringType:
					do_op_string(child0->v.string, child1->v.string, this->op);
					break;
				case MincHandleType:
					minc_warn("can't operate on a string and a handle");
					break;
				case MincListType:
					minc_warn("can't operate on a string and a list");
					break;
				default:
					minc_internal_error("operator %s: invalid rhs type: %s", printOpKind(this->op), MincTypeName(child1->type));
					break;
			}
			break;
		case MincHandleType:
			switch (child1->type) {
				case MincFloatType:
					do_op_handle_num(child0->v.handle, child1->v.number, this->op);
					break;
				case MincStringType:
					minc_warn("can't operate on a string and a handle");
					break;
				case MincHandleType:
					do_op_handle_handle(child0->v.handle, child1->v.handle, this->op);
					break;
				case MincListType:
					minc_warn("can't operate on a list and a handle");
					break;
				default:
					minc_internal_error("operator %s: invalid rhs type: %s", printOpKind(this->op), MincTypeName(child1->type));
					break;
			}
			break;
		case MincListType:
			switch (child1->type) {
				case MincFloatType:
					do_op_list_iterate(child0, child1->v.number, this->op);
					break;
				case MincStringType:
					minc_warn("can't operate on a string");
					break;
				case MincHandleType:
					minc_warn("can't operate on a handle");
					break;
				case MincListType:
					do_op_list_list(child0, child1, this->op);
					break;
				default:
					minc_internal_error("operator %s: invalid rhs type: %s", printOpKind(this->op), MincTypeName(child1->type));
					break;
			}
			break;
		default:
		 minc_internal_error("operator %s: invalid lhs type: %s", printOpKind(this->op), MincTypeName(child0->type));
			break;
	}
	return this;
}

Node *	NodeUnaryOperator::doExct()
{
	this->type = MincFloatType;
	if (this->op == OpNeg)
		this->v.number = -child(0)->exct()->v.number;
	return this;
}

Node *	NodeOr::doExct()
{
	this->type = MincFloatType;
	this->v.number = 0.0;
	if ((cmp(0.0, child(0)->exct()->v.number) != 0) ||
		(cmp(0.0, child(1)->exct()->v.number) != 0)) {
		this->v.number = 1.0;
	}
	return this;
}

Node *	NodeIf::doExct()
{
	if (cmp(0.0, child(0)->exct()->v.number) != 0)
		child(1)->exct();
	return this;
}

Node *	NodeIfElse::doExct()
{
	if (cmp(0.0, child(0)->exct()->v.number) != 0)
		child(1)->exct();
	else
		child(2)->exct();
	return this;
}

Node *	NodeWhile::doExct()
{
	while (cmp(0.0, child(0)->exct()->v.number) != 0)
		child(1)->exct();
	return this;
}

Node *	NodeArgList::doExct()
{
	sArgListLen = 0;
	sArgListIndex = 0;	// reset to walk list
	inCalledFunctionArgList = true;
	TPRINT("NodeArgList: walking function '%s()' arg decl/copy list\n", sCalledFunction);
	child(0)->exct();
	inCalledFunctionArgList = false;
	return this;
}

Node *	NodeArgListElem::doExct()
{
	++sArgListLen;
	child(0)->exct();	// work our way to the front of the list
	child(1)->exct();	// run the arg decl
	// Symbol associated with this function argument
	Symbol *argSym = child(1)->u.symbol;
	if (sMincListLen > sArgListLen) {
		minc_die("%s() takes %d arguments but was passed %d!", sCalledFunction, sArgListLen, sMincListLen);
	}
	else if (sArgListIndex >= sMincListLen) {
		minc_warn("%s(): arg '%s' not provided - defaulting to 0", sCalledFunction, argSym->name);
		/* Copy zeroed MincValue union to us and then to sym. */
		MincListElem zeroElem;
		zeroElem.type = argSym->type;
		memset(&zeroElem.val, 0, sizeof(MincValue));
		copy_listelem_tree(this, &zeroElem);
		copy_tree_sym(argSym, this);
		++sArgListIndex;
	}
	/* compare stored NodeName with user-passed arg */
	else {
		// Pre-cached argument value from caller
		MincListElem *argValue = &sMincList[sArgListIndex];
		bool compatible = false;
		switch (argValue->type) {
			case MincFloatType:
			case MincStringType:
			case MincHandleType:
			case MincListType:
				if (argSym->type != argValue->type) {
					minc_die("%s() arg '%s' passed as %s, expecting %s",
								sCalledFunction, argSym->name, MincTypeName(argValue->type), MincTypeName(argSym->type));
				}
				else compatible = true;
				break;
			default:
				assert(argValue->type != MincVoidType);
				break;
		}
		if (compatible) {
			/* Copy passed-in arg's MincValue union to us and then to sym. */
			copy_listelem_tree(this, argValue);
			copy_tree_sym(argSym, this);
		}
		++sArgListIndex;
	}
	return this;
}

Node *	NodeRet::doExct()
{
	child(0)->exct();
	copy_tree_tree(this, child(0));
	assert(this->type == child(0)->type);
	TPRINT("NodeRet throwing %p for return stmt\n", this);
	throw this;	// Cool, huh?  Throws this node's body out to function's endpoint!
	return NULL;	// notreached
}

Node *	NodeFuncSeq::doExct()
{
	child(0)->exct();
	child(1)->exct();
	copy_tree_tree(this, child(1));
	assert(this->type == child(1)->type);
	return this;
}

Node *	NodeFor::doExct()
{
	child(0)->exct();         /* init */
	while (cmp(0.0, child(1)->exct()->v.number) != 0) { /* condition */
		_child4->exct();      /* execute block */
		child(2)->exct();      /* prepare for next iteration */
	}
	return this;
}

Node *	NodeSeq::doExct()
{
	child(0)->exct();
	child(1)->exct();
	return this;
}

Node *	NodeBlock::doExct()
{
	push_scope();
	child(0)->exct();
	pop_scope();
	return this;				// NodeBlock returns void type
}

Node *	NodeDecl::doExct()
{
	TPRINT("-- declaring variable '%s'\n", _symbolName);
	Symbol *sym = lookup(_symbolName, inCalledFunctionArgList ? ThisLevel : AnyLevel);
	if (!sym) {
		sym = install(_symbolName, NO);
		sym->type = this->type;
	}
	else {
		if (sym->scope == current_scope()) {
			if (inCalledFunctionArgList) {
				minc_die("%s(): argument variable '%s' already used", sCalledFunction, _symbolName);
			}
			minc_warn("variable '%s' redefined - using existing one", _symbolName);
		}
		else {
			if (sFunctionCallDepth == 0) {
				minc_warn("variable '%s' also defined at enclosing scope", _symbolName);
			}
			sym = install(_symbolName, NO);
			sym->type = this->type;
		}
	}
	this->u.symbol = sym;
	return this;
}

Node *	NodeFuncDecl::doExct()
{
	TPRINT("-- declaring function '%s'\n", _symbolName);
	assert(current_scope() == 0);	// until I allow nested functions
	Symbol *sym = lookup(_symbolName, GlobalLevel);	// only look at current global level
	if (sym == NULL) {
		sym = install(_symbolName, YES);		// all functions global for now
		sym->type = this->type;
		this->u.symbol = sym;
	}
	else {
		minc_die("function %s() is already declared", _symbolName);
	}
	return this;
}

Node *	NodeFuncDef::doExct()
{
	// Look up symbol for function, and bind this FuncDef node to it.
	TPRINT("NodeFuncDef: executing lookup node %p\n", child(0));
	child(0)->exct();
	assert(child(0)->u.symbol != NULL);
	child(0)->u.symbol->node = this;
	return this;
}

static void
clear_list(MincListElem *list, int len)
{
	ENTER();
	int i;
	for (i = 0; i < len; ++i) {
		clear_elem(&list[i]);
	}
}

static void
push_list()
{
	ENTER();
   if (list_stack_ptr >= MAXSTACK)
      minc_die("stack overflow: too many nested function calls");
   list_stack[list_stack_ptr] = sMincList;
   list_len_stack[list_stack_ptr++] = sMincListLen;
   sMincList = (MincListElem *) calloc(MAXDISPARGS, sizeof(MincListElem));
   TPRINT("push_list: sMincList=%p at stack level %d, len %d\n", sMincList, list_stack_ptr, sMincListLen);
   sMincListLen = 0;
}


static void
pop_list()
{
	ENTER();
   TPRINT("pop_list: sMincList=%p\n", sMincList);
   clear_list(sMincList, MAXDISPARGS);
   efree(sMincList);
   if (list_stack_ptr == 0)
      minc_die("stack underflow");
   sMincList = list_stack[--list_stack_ptr];
   sMincListLen = list_len_stack[list_stack_ptr];
	TPRINT("pop_list: now at sMincList=%p, stack level %d, len %d\n", sMincList, list_stack_ptr, sMincListLen);
}

static void copy_value(MincValue *dest, MincDataType destType,
                       MincValue *src, MincDataType srcType)
{
	ENTER();
   if (srcType == MincHandleType && src->handle != NULL) {
      ref_handle(src->handle);	// ref before unref
   }
   else if (srcType == MincListType && src->list != NULL) {
#ifdef DEBUG_MEMORY
      TPRINT("list %p refcount %d -> %d\n", src->list, src->list->refcount, src->list->refcount+1);
#endif
      ++src->list->refcount;
   }
   if (destType == MincHandleType && dest->handle != NULL) {
      TPRINT("\toverwriting existing Handle value\n");
      unref_handle(dest->handle);	// overwriting handle, so unref
   }
   else if (destType == MincListType && dest->list != NULL) {
      TPRINT("\toverwriting existing MincList value\n");
      unref_value_list(dest);
   }
   memcpy(dest, src, sizeof(MincValue));
}

/* This copies a node's value and handles ref counting when necessary */
static void
copy_tree_tree(Node *tpdest, Node *tpsrc)
{
   TPRINT("copy_tree_tree(%p, %p)\n", tpdest, tpsrc);
#ifdef EMBEDDED
	/* Not yet handling errors with throw/catch */
	if (tpsrc->type == MincVoidType) {
		return;
	}
#endif
   copy_value(&tpdest->v, tpdest->type, &tpsrc->v, tpsrc->type);
	if (tpdest->type != MincVoidType && tpsrc->type != tpdest->type) {
		minc_warn("Overwriting %s variable '%s' with %s", MincTypeName(tpdest->type), tpdest->name(), MincTypeName(tpsrc->type));
	}
   tpdest->type = tpsrc->type;
	TPRINT("dest: ");
	print_value(&tpdest->v, tpdest->type);
}

/* This copies a Symbol's value and handles ref counting when necessary */
static void
copy_sym_tree(Node *tpdest, Symbol *src)
{
   TPRINT("copy_sym_tree(%p, %p)\n", tpdest, src);
#ifdef EMBEDDED
	/* Not yet handling errors with throw/catch */
	if (src == NULL) {
		return;
	}
#endif
	assert(src->scope != -1);	// we accessed a variable after leaving its scope!
   copy_value(&tpdest->v, tpdest->type, &src->v, src->type);
	if (tpdest->type != MincVoidType && src->type != tpdest->type) {
		minc_warn("Overwriting %s variable '%s' with %s", MincTypeName(tpdest->type), tpdest->name(), MincTypeName(src->type));
	}
   tpdest->type = src->type;
	TPRINT("dest: ");
	print_value(&tpdest->v, tpdest->type);
}

static void
copy_tree_sym(Symbol *dest, Node *tpsrc)
{
	TPRINT("copy_tree_sym(%p, %p)\n", dest, tpsrc);
#ifdef EMBEDDED
	/* Not yet handling errors using throw/catch */
	if (dest == NULL || tpsrc->type == MincVoidType) {
		return;
	}
#endif
	assert(dest->scope != -1);	// we accessed a variable after leaving its scope!
   copy_value(&dest->v, dest->type, &tpsrc->v, tpsrc->type);
	if (dest->type != MincVoidType && tpsrc->type != dest->type) {
		minc_warn("Overwriting %s variable '%s' with %s", MincTypeName(dest->type), dest->name, MincTypeName(tpsrc->type));
	}
   dest->type = tpsrc->type;
}

static void
copy_tree_listelem(MincListElem *dest, Node *tpsrc)
{
   TPRINT("copy_tree_listelem(%p, %p)\n", dest, tpsrc);
#ifdef EMBEDDED
	/* Not yet handling errors with throw/catch */
	if (tpsrc->type == MincVoidType) {
		return;
	}
#endif
   copy_value(&dest->val, dest->type, &tpsrc->v, tpsrc->type);
   dest->type = tpsrc->type;
}

static void
copy_listelem_tree(Node *tpdest, MincListElem *esrc)
{
   TPRINT("copy_listelem_tree(%p, %p)\n", tpdest, esrc);
   copy_value(&tpdest->v, tpdest->type, &esrc->val, esrc->type);
   tpdest->type = esrc->type;
}

static void
copy_listelem_elem(MincListElem *edest, MincListElem *esrc)
{
   TPRINT("copy_listelem_elem(%p, %p)\n", edest, esrc);
   copy_value(&edest->val, edest->type, &esrc->val, esrc->type);
   edest->type = esrc->type;
}

void print_tree(Node *tp)
{
#ifdef DEBUG
	rtcmix_print("Node %p: %s type: %d\n", tp, printNodeKind(tp->kind), tp->type);
	if (tp->kind == eNodeName) {
		rtcmix_print("Symbol:\n");
		print_symbol(tp->u.symbol);
	}
	else if (tp->type == MincVoidType && tp->child(0) != NULL) {
		rtcmix_print("Child 0:\n");
		print_tree(tp->child(0));
	}
#endif
}

#ifdef DEBUG
static void print_symbol(Symbol * s)
{
	rtcmix_print("Symbol %p: '%s' scope: %d type: %d\n", s, s->name, s->scope, s->type);
}
#endif

static void print_value(MincValue *v, MincDataType type)
{
	switch (type) {
		case MincFloatType:
			TPRINT("%f\n", v->number);
			break;
		case MincHandleType:
			TPRINT("%p\n", v->handle);
			break;
		case MincListType:
			TPRINT("%p\n", v->list);
			break;
		case MincStringType:
			TPRINT("%s\n", v->string);
			break;
		case MincVoidType:
			TPRINT("void\n");
			break;
	}
}

void
clear_elem(MincListElem *elem)
{
	if (elem->type == MincListType) {
	   TPRINT("clear_elem(%p)\n", elem);
       unref_value_list(&elem->val);
	}
	else if (elem->type == MincHandleType) {
	   TPRINT("clear_elem(%p)\n", elem);
	   unref_handle(elem->val.handle);
	}
}


void
unref_value_list(MincValue *value)
{
	if (value->list == NULL)
		return;
#ifdef DEBUG_MEMORY
   TPRINT("unref_value_list(%p) [%d -> %d]\n", value->list, value->list->refcount, value->list->refcount-1);
#endif
   assert(value->list->refcount > 0);
   if (--value->list->refcount == 0) {
      if (value->list->data != NULL) {
		 int e;
		 TPRINT("\tfreeing MincList data %p...\n", value->list->data);
		 for (e = 0; e < value->list->len; ++e) {
			 MincListElem *elem = &value->list->data[e];
			 clear_elem(elem);
		 }
		 efree(value->list->data);
		 value->list->data = NULL;
		 TPRINT("\tdone\n");
      }
	  TPRINT("\tfreeing MincList %p\n", value->list);
	  efree(value->list);
   }
}

