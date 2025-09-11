// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*===========================================================================

FILE:  AEEQList.h

GENERAL DESCRIPTION:  Doubly-linked circular list implementation

===========================================================================*/
#ifndef _AEEQLIST_H_
#define _AEEQLIST_H_

typedef struct QNode QNode;
struct QNode {
	QNode *pNext;
	QNode *pPrev;
};

#define QLIST_DEFINE_INIT(f) QList f = { { &f.n, &f.n } }

typedef struct QList QList;
struct QList {
	QNode n;
};

static __inline void
QNode_InsPrev(QNode *me, QNode *pn)
{
	QNode *pPrev = me->pPrev;

	pn->pNext = me;
	pn->pPrev = pPrev;
	pPrev->pNext = pn;
	me->pPrev = pn;
}

static __inline void
QNode_InsNext(QNode *me, QNode *pn)
{
	QNode *pNext = me->pNext;

	pn->pPrev = me;
	pn->pNext = pNext;
	pNext->pPrev = pn;
	me->pNext = pn;
}

static __inline void
QNode_Dequeue(QNode *me)
{
	QNode *pNext = me->pNext;
	QNode *pPrev = me->pPrev;

	pPrev->pNext = pNext;
	pNext->pPrev = pPrev;
}

static __inline void
QNode_CtorZ(QNode *me)
{
	me->pNext = me->pPrev = 0;
}

static __inline int
QNode_IsQueuedZ(QNode *me)
{
	return (0 != me->pNext);
}

static __inline void
QNode_DequeueZ(QNode *me)
{
	if(QNode_IsQueuedZ(me)) {
		QNode_Dequeue(me);
		me->pNext = me->pPrev = 0;
	}
}

//--------------------------------------------------------------------
//--  QList functions  ----------------------------------------------
//--------------------------------------------------------------------

static __inline void
QList_Zero(QList *me)
{
	me->n.pNext = me->n.pPrev = &me->n;
}

static __inline void
QList_Ctor(QList *me)
{
	QList_Zero(me);
}

static __inline int
QList_IsEmpty(QList *me)
{
	return me->n.pNext == &me->n;
}

static __inline int
QList_IsNull(QList *me)
{
	return ((0 == me->n.pNext) && (0 == me->n.pPrev));
}

static __inline void
QList_AppendNode(QList *me, QNode *pn)
{
	QNode_InsPrev(&me->n, pn);
}

static __inline void
QList_PrependNode(QList *me, QNode *pn)
{
	QNode_InsNext(&me->n, pn);
}

static __inline void
QList_CtorFrom(QList *me, QList *psrc)
{
	QNode *s = &psrc->n;
	QNode *d = &me->n;

	s->pNext->pPrev = d;
	d->pPrev = s->pPrev;
	d->pNext = s->pNext;
	s->pPrev->pNext = d;

	QList_Zero(psrc);
}

static __inline void
QList_AppendList(QList *me, QList *psrc)
{
	QNode *s = &psrc->n;
	QNode *d = &me->n;
	QNode *dp = d->pPrev;
	QNode *sn = s->pNext;
	QNode *sp;

	sn->pPrev = dp;
	dp->pNext = sn;
	d->pPrev = (sp = s->pPrev);
	sp->pNext = d;

	QList_Zero(psrc);
}

#define QLIST_FOR_ALL(pList, pNode)                                           \
	for((pNode) = (pList)->n.pNext; (pNode) != &(pList)->n;               \
	    (pNode) = (pNode)->pNext)

#define QLIST_FOR_REST(pList, pNode)                                          \
	for(; (pNode) != &(pList)->n; (pNode) = (pNode)->pNext)

#define QLIST_REV_FOR_ALL(pList, pNode)                                       \
	for((pNode) = (pList)->n.pPrev; (pNode) != &(pList)->n;               \
	    (pNode) = (pNode)->pPrev)

#define QLIST_REV_FOR_REST(pList, pNode)                                      \
	for(; (pNode) != &(pList)->n; (pNode) = (pNode)->pPrev)

/* Allows dequeing QNodes during iteration */
#define QLIST_NEXTSAFE_FOR_ALL(pList, pNode, pNodeNext)                       \
	for((pNode) = (pList)->n.pNext, (pNodeNext) = (pNode)->pNext;         \
	    (pNode) != &(pList)->n;                                           \
	    (pNode) = (pNodeNext), (pNodeNext) = (pNode)->pNext)

static __inline QNode *
QList_GetFirst(QList *me)
{
	QNode *pn = me->n.pNext;

	return (pn == &me->n ? 0 : pn);
}

static __inline QNode *
QList_GetLast(QList *me)
{
	QNode *pn = me->n.pPrev;

	return (pn == &me->n ? 0 : pn);
}

static __inline QNode *
QList_Pop(QList *me)
{
	QNode *pn = me->n.pNext;
	QNode *pnn = pn->pNext;

	me->n.pNext = pnn;
	pnn->pPrev = &me->n;

	return (pn == &me->n ? 0 : pn);
}

static __inline QNode *
QList_PopZ(QList *me)
{
	QNode *pn = QList_Pop(me);
	if(0 != pn) {
		QNode_CtorZ(pn);
	}
	return pn;
}

static __inline QNode *
QList_PopLast(QList *me)
{
	QNode *pp = me->n.pPrev;
	QNode *ppp = pp->pPrev;

	me->n.pPrev = ppp;
	ppp->pNext = &me->n;

	return (pp == &me->n ? 0 : pp);
}

static __inline QNode *
QList_PopLastZ(QList *me)
{
	QNode *pn = QList_PopLast(me);
	if(0 != pn) {
		QNode_CtorZ(pn);
	}
	return pn;
}

/*=====================================================================
=======================================================================
DATA STRUCTURE DOCUMENTATION
=======================================================================

QNode

Description:
 Qnode is the structure that is queued.  One or more Qnodes may be
 embedded in other structures. An object can contain multiple QNodes if
 it needs to be in different lists at the same time.

Definition:

   typedef struct QNode QNode;
   struct QNode {
      QNode *pNext;
      QNode *pPrev;
   };

Members:

See Also:

=======================================================================

QList

Description:
 QList keeps a doubly-linked list of QNode structures.
 Each queue is represented by a 'head' node, not a head pointer,
 simplifying and streamlining many operations.
 Because it is doubly-linked it permits constant-time insertion or removal
 of items or of entire queues.
 Because it is circular it permits constant-time operations at both the
 tail and the head of the queue.  Circularity also streamlines some
 operations by eliminating conditional branches.

 General rules:
 QLists are always in a defined state; they should be constructed
 before use, using one of the supplied Ctor...() functions.
 QNodes do not track queued vs. unqueued state.  The client should
 never dequeue an un-queued node or queue an already-queued node.
 When not queued, QNode internal state is undefined.   A client may
 implement marking and assertion externally.

Definition:

   typedef struct QList QList;
   struct QList {
      QNode n;
   };

Members:

See Also:

=======================================================================
INTERFACE DOCUMENTATION
=======================================================================
QNode Interface

  QNode is a statically-linked interface.

=======================================================================
QNode_CtorZ()

Description:
   Zero initialize a QNode.

Prototype:

   void QNode_CtorZ(QNode *me);

Parameters:
   me: the QNode

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   QNode_IsQueued(), QNode_DequeueZ(), QList_PopZ()

=======================================================================
QNode_IsQueuedZ()

Description:
   Whether a QNode belongs in a Queue.

Prototype:

   int QNode_IsQueuedZ(QNode *me);

Parameters:
   me: the QNode

Return Value:
   None

Comments:
   None

Side Effects:
   Does not work if a node needs to live at address 0x0.

See Also:
   QNode_CtorZ(), QNode_DequeueZ(), QList_PopZ()

=======================================================================
QNode_DequeueZ()

Description:
   Dequeue a QNode if it is in a queue. Idempotent operation.

Prototype:

   void QNode_DequeueZ(QNode *me);

Parameters:
   me: the QNode

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   QNode_CtorZ(), QNode_IsQueued(), QList_PopZ()

=======================================================================

QNode_InsPrev()

Description:
        insert a node before this one.

Prototype:
        static __inline void QNode_InsPrev(QNode *me, QNode *pn)

Parameters:
        me: the QNode
        pn: the node to be inserted.
Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================

QNode_InsNext()

Description:
        insert a node after this one.

Prototype:
        static __inline void QNode_InsNext(QNode *me, QNode *pn)

Parameters:
        me: the QNode
        pn: the node to be inserted.

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QNode_Dequeue()

Description:
        dequeue this node.

Prototype:
        static __inline void QNode_Dequeue(QNode *me)

Parameters:
        me: the QNode to be dequeued

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList Interface

  QList is a statically-linked interface.  It provides a Queue of
  doubly linked nodes.

=======================================================================
QList_Zero()

Description:
   discard all queued nodes.

Prototype:

   void QList_Zero(QList *me)

Parameters:
   me: the QList

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_Ctor()

Description:
   Initialize a queue to an empty state

Prototype:

   void QList_Ctor(QList *me)

Parameters:
   me: the QList

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_IsEmpty()

Description:
   Check whether queue is empty.

Prototype:

   int QList_IsEmpty(QList *me)

Parameters:
   me: the QList

Return Value:
   TRUE if queue is empty.

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_AppendNode()

Description:
   Append the node to the queue. Make it the last 'next' (and the
   first 'prev')

Prototype:

   void QList_AppendNode(QList *me, QNode *pn)

Parameters:
   me: the QList
   pn: the node to append.

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_PrependNode()

Description:
   Prepend a node to the queue. Make it the first 'next' (and the
   last 'prev').

Prototype:

   void QList_PrependNode(QList *me, QNode *pn)

Parameters:
   me: the QList
   pn: the node to prepend.

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_CtorFrom()

Description:
   Move nodes from one queue to a newly constructed queue.
   Weird aliasing voodoo allows this to work without conditional branches, even
   when psrc is empty.  In that case, "s->pNext->pPrev = d" overwrites s->pPrev
with d, so that "s->pPrev->pNext = d" will later overwrite d->pNext with d.

Prototype:

   void QList_CtorFrom(QList *me, QList *psrc)

Parameters:
   me: the QList
   psrc: the Qlist from

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_AppendList()

Description:
  Move all nodes from a source queue to the end of this queue.
  Note that weird aliasing voodoo allows this to work without conditional
  branches when psrc is empty.  A summary:

      SNP = DP      =>  SP = DP, because SNP aliases SP
      DPN = SN      =>  DPN = S
      DP = SP       =>  DP = DP, because SP was overwritten with DP
      SPN = D       =>  DPN = D

Prototype:

   void QList_AppendList(QList *me, QList *psrc)

Parameters:
   me: the QList
   psrc: the source Qlist.

Return Value:
   None

Comments:
   None

Side Effects:
   None

See Also:
   None

=======================================================================
QList_GetFirst()

Description:
   Get the first item on the queue

Prototype:

   QNode *QList_GetFirst(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty.

Comments:
   None

Side Effects:
   None

See Also:
   QList_GetLast

=======================================================================
QList_GetLast()

Description:
   Get the last item on the queue

Prototype:

   QNode *QList_GetLast(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty.

Comments:
   None

Side Effects:
   None

See Also:
   QList_GetFirst

=======================================================================
QList_Pop()

Description:
   Remove and return the first item on the queue (FIFO).

Prototype:

   QNode *QList_Pop(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty

Comments:
   None

Side Effects:
   None

See Also:
   QNode_PopZ, QNode_PopLast(), QNode_PopLastZ, QNode_CtorZ(),
QNode_IsQueued(), QNode_DequeueZ()

=======================================================================
QList_PopZ()

Description:
   Remove and return the first item on the queue (FIFO). Same as QList_Pop(),
   except the node retured is zero-initialized.

Prototype:

   QNode *QList_PopZ(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty

Comments:
   None

Side Effects:
   None

See Also:
   QNode_Pop, QNode_PopLast(), QNode_PopLastZ, QNode_CtorZ(), QNode_IsQueued(),
   QNode_DequeueZ()

=======================================================================
QList_PopLast()

Description:
   Remove and return the first item on the queue (FILO).

Prototype:

   QNode *QList_PopLast(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty

Comments:
   None

Side Effects:
   None

See Also:
   QNode_PopLastZ, QNode_Pop(), QNode_PopZ, QNode_CtorZ(), QNode_IsQueued(),
   QNode_DequeueZ()

=======================================================================

QList_IsNull()

Description:
Checks if the QList is null or not.

Prototype:
static __inline int QList_IsNull(QList *me)

Parameters:
   me: the QList

Return Value:
   True or False.

Comments:
   None

Side Effects:
   None

See Also:
        None

=======================================================================

QList_PopLastZ()

Description:
   Remove and return the first item on the queue (FILO).
   Same as QList_PopLast(), except the node retured is zero-initialized.

Prototype:

   QNode *QList_PopLastZ(QList *me)

Parameters:
   me: the QList

Return Value:
   pointer to QNode or 0 if queue is empty

Comments:
   None

Side Effects:
   None

See Also:
   QNode_Pop(), QNode_PopZ, QNode_CtorZ(), QNode_IsQueued(), QNode_DequeueZ()

=====================================================================*/
#endif // _AEEQLIST_H_
