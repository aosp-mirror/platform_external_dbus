/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-hash.c  Generic hash table utility (internal to D-BUS implementation)
 * 
 * Copyright (C) 2002  Red Hat, Inc.
 * Copyright (c) 1991-1993 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 * 
 * Hash table implementation based on generic/tclHash.c from the Tcl
 * source code. The original Tcl license applies to portions of the
 * code from tclHash.c; the Tcl license follows this standad D-BUS
 * license information.
 *
 * Licensed under the Academic Free License version 1.2
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/* 
 * The following copyright applies to code from the Tcl distribution.
 *
 * Copyright (c) 1991-1993 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 *
 * This software is copyrighted by the Regents of the University of
 * California, Sun Microsystems, Inc., Scriptics Corporation, and
 * other parties.  The following terms apply to all files associated
 * with the software unless explicitly disclaimed in individual files.
 * 
 * The authors hereby grant permission to use, copy, modify,
 * distribute, and license this software and its documentation for any
 * purpose, provided that existing copyright notices are retained in
 * all copies and that this notice is included verbatim in any
 * distributions. No written agreement, license, or royalty fee is
 * required for any of the authorized uses.  Modifications to this
 * software may be copyrighted by their authors and need not follow
 * the licensing terms described here, provided that the new terms are
 * clearly indicated on the first page of each file where they apply.
 * 
 * IN NO EVENT SHALL THE AUTHORS OR DISTRIBUTORS BE LIABLE TO ANY
 * PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
 * DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE, ITS DOCUMENTATION,
 * OR ANY DERIVATIVES THEREOF, EVEN IF THE AUTHORS HAVE BEEN ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHORS AND DISTRIBUTORS SPECIFICALLY DISCLAIM ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND
 * NON-INFRINGEMENT.  THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS,
 * AND THE AUTHORS AND DISTRIBUTORS HAVE NO OBLIGATION TO PROVIDE
 * MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 * 
 * GOVERNMENT USE: If you are acquiring this software on behalf of the
 * U.S. government, the Government shall have only "Restricted Rights"
 * in the software and related documentation as defined in the Federal
 * Acquisition Regulations (FARs) in Clause 52.227.19 (c) (2).  If you
 * are acquiring the software on behalf of the Department of Defense,
 * the software shall be classified as "Commercial Computer Software"
 * and the Government shall have only "Restricted Rights" as defined
 * in Clause 252.227-7013 (c) (1) of DFARs.  Notwithstanding the
 * foregoing, the authors grant the U.S. Government and others acting
 * in its behalf permission to use and distribute the software in
 * accordance with the terms specified in this license.
 */

#include "dbus-memory.h"

typedef struct DBusHashEntry DBusHashEntry;


#if 0

/*
 * Forward declaration of Tcl_HashTable.  Needed by some C++ compilers
 * to prevent errors when the forward reference to Tcl_HashTable is
 * encountered in the Tcl_HashEntry structure.
 */

#ifdef __cplusplus
struct Tcl_HashTable;
#endif

/*
 * Structure definition for an entry in a hash table.  No-one outside
 * Tcl should access any of these fields directly;  use the macros
 * defined below.
 */

typedef struct Tcl_HashEntry {
    struct Tcl_HashEntry *nextPtr;	/* Pointer to next entry in this
					 * hash bucket, or NULL for end of
					 * chain. */
    struct Tcl_HashTable *tablePtr;	/* Pointer to table containing entry. */
    struct Tcl_HashEntry **bucketPtr;	/* Pointer to bucket that points to
					 * first entry in this entry's chain:
					 * used for deleting the entry. */
    ClientData clientData;		/* Application stores something here
					 * with Tcl_SetHashValue. */
    union {				/* Key has one of these forms: */
	char *oneWordValue;		/* One-word value for key. */
	int words[1];			/* Multiple integer words for key.
					 * The actual size will be as large
					 * as necessary for this table's
					 * keys. */
	char string[4];			/* String for key.  The actual size
					 * will be as large as needed to hold
					 * the key. */
    } key;				/* MUST BE LAST FIELD IN RECORD!! */
} Tcl_HashEntry;

/*
 * Structure definition for a hash table.  Must be in tcl.h so clients
 * can allocate space for these structures, but clients should never
 * access any fields in this structure.
 */

#define TCL_SMALL_HASH_TABLE 4
typedef struct Tcl_HashTable {
    Tcl_HashEntry **buckets;		/* Pointer to bucket array.  Each
					 * element points to first entry in
					 * bucket's hash chain, or NULL. */
    Tcl_HashEntry *staticBuckets[TCL_SMALL_HASH_TABLE];
					/* Bucket array used for small tables
					 * (to avoid mallocs and frees). */
    int numBuckets;			/* Total number of buckets allocated
					 * at **bucketPtr. */
    int numEntries;			/* Total number of entries present
					 * in table. */
    int rebuildSize;			/* Enlarge table when numEntries gets
					 * to be this large. */
    int downShift;			/* Shift count used in hashing
					 * function.  Designed to use high-
					 * order bits of randomized keys. */
    int mask;				/* Mask value used in hashing
					 * function. */
    int keyType;			/* Type of keys used in this table. 
					 * It's either TCL_STRING_KEYS,
					 * TCL_ONE_WORD_KEYS, or an integer
					 * giving the number of ints that
                                         * is the size of the key.
					 */
    Tcl_HashEntry *(*findProc) _ANSI_ARGS_((struct Tcl_HashTable *tablePtr,
	    CONST char *key));
    Tcl_HashEntry *(*createProc) _ANSI_ARGS_((struct Tcl_HashTable *tablePtr,
	    CONST char *key, int *newPtr));
} Tcl_HashTable;

/*
 * Structure definition for information used to keep track of searches
 * through hash tables:
 */

typedef struct Tcl_HashSearch {
    Tcl_HashTable *tablePtr;		/* Table being searched. */
    int nextIndex;			/* Index of next bucket to be
					 * enumerated after present one. */
    Tcl_HashEntry *nextEntryPtr;	/* Next entry to be enumerated in the
					 * the current bucket. */
} Tcl_HashSearch;


/*
 * When there are this many entries per bucket, on average, rebuild
 * the hash table to make it larger.
 */

#define REBUILD_MULTIPLIER	3


/*
 * The following macro takes a preliminary integer hash value and
 * produces an index into a hash tables bucket list.  The idea is
 * to make it so that preliminary values that are arbitrarily similar
 * will end up in different buckets.  The hash function was taken
 * from a random-number generator.
 */

#define RANDOM_INDEX(tablePtr, i) \
    (((((long) (i))*1103515245) >> (tablePtr)->downShift) & (tablePtr)->mask)

/*
 * Procedure prototypes for static procedures in this file:
 */

static Tcl_HashEntry *	ArrayFind (Tcl_HashTable *tablePtr,
                                   CONST char *key);
static Tcl_HashEntry *	ArrayCreate (Tcl_HashTable *tablePtr,
                                     CONST char *key, int *newPtr);
static Tcl_HashEntry *	BogusFind (Tcl_HashTable *tablePtr,
                                   CONST char *key);
static Tcl_HashEntry *	BogusCreate (Tcl_HashTable *tablePtr,
                                     CONST char *key, int *newPtr);
static unsigned int	HashString (CONST char *string);
static void		RebuildTable (Tcl_HashTable *tablePtr);
static Tcl_HashEntry *	StringFind (Tcl_HashTable *tablePtr,
                                    CONST char *key);
static Tcl_HashEntry *	StringCreate (Tcl_HashTable *tablePtr,
                                      CONST char *key, int *newPtr);
static Tcl_HashEntry *	OneWordFind (Tcl_HashTable *tablePtr,
                                     CONST char *key);
static Tcl_HashEntry *	OneWordCreate (Tcl_HashTable *tablePtr,
                                       CONST char *key, int *newPtr);

/*
 *----------------------------------------------------------------------
 *
 * Tcl_InitHashTable --
 *
 *	Given storage for a hash table, set up the fields to prepare
 *	the hash table for use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	TablePtr is now ready to be passed to Tcl_FindHashEntry and
 *	Tcl_CreateHashEntry.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_InitHashTable(tablePtr, keyType)
     register Tcl_HashTable *tablePtr;	/* Pointer to table record, which
					 * is supplied by the caller. */
     int keyType;			/* Type of keys to use in table:
					 * TCL_STRING_KEYS, TCL_ONE_WORD_KEYS,
					 * or an integer >= 2. */
{
#if (TCL_SMALL_HASH_TABLE != 4) 
  panic("Tcl_InitHashTable: TCL_SMALL_HASH_TABLE is %d, not 4\n",
        TCL_SMALL_HASH_TABLE);
#endif
    
  tablePtr->buckets = tablePtr->staticBuckets;
  tablePtr->staticBuckets[0] = tablePtr->staticBuckets[1] = 0;
  tablePtr->staticBuckets[2] = tablePtr->staticBuckets[3] = 0;
  tablePtr->numBuckets = TCL_SMALL_HASH_TABLE;
  tablePtr->numEntries = 0;
  tablePtr->rebuildSize = TCL_SMALL_HASH_TABLE*REBUILD_MULTIPLIER;
  tablePtr->downShift = 28;
  tablePtr->mask = 3;
  tablePtr->keyType = keyType;
  if (keyType == TCL_STRING_KEYS) {
    tablePtr->findProc = StringFind;
    tablePtr->createProc = StringCreate;
  } else if (keyType == TCL_ONE_WORD_KEYS) {
    tablePtr->findProc = OneWordFind;
    tablePtr->createProc = OneWordCreate;
  } else {
    tablePtr->findProc = ArrayFind;
    tablePtr->createProc = ArrayCreate;
  };
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_DeleteHashEntry --
 *
 *	Remove a single entry from a hash table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The entry given by entryPtr is deleted from its table and
 *	should never again be used by the caller.  It is up to the
 *	caller to free the clientData field of the entry, if that
 *	is relevant.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_DeleteHashEntry(entryPtr)
     Tcl_HashEntry *entryPtr;
{
  register Tcl_HashEntry *prevPtr;

  if (*entryPtr->bucketPtr == entryPtr) {
    *entryPtr->bucketPtr = entryPtr->nextPtr;
  } else {
    for (prevPtr = *entryPtr->bucketPtr; ; prevPtr = prevPtr->nextPtr) {
      if (prevPtr == NULL) {
        panic("malformed bucket chain in Tcl_DeleteHashEntry");
      }
      if (prevPtr->nextPtr == entryPtr) {
        prevPtr->nextPtr = entryPtr->nextPtr;
        break;
      }
    }
  }
  entryPtr->tablePtr->numEntries--;
  ckfree((char *) entryPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_DeleteHashTable --
 *
 *	Free up everything associated with a hash table except for
 *	the record for the table itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The hash table is no longer useable.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_DeleteHashTable(tablePtr)
     register Tcl_HashTable *tablePtr;		/* Table to delete. */
{
  register Tcl_HashEntry *hPtr, *nextPtr;
  int i;

  /*
   * Free up all the entries in the table.
   */

  for (i = 0; i < tablePtr->numBuckets; i++) {
    hPtr = tablePtr->buckets[i];
    while (hPtr != NULL) {
      nextPtr = hPtr->nextPtr;
      ckfree((char *) hPtr);
      hPtr = nextPtr;
    }
  }

  /*
   * Free up the bucket array, if it was dynamically allocated.
   */

  if (tablePtr->buckets != tablePtr->staticBuckets) {
    ckfree((char *) tablePtr->buckets);
  }

  /*
   * Arrange for panics if the table is used again without
   * re-initialization.
   */

  tablePtr->findProc = BogusFind;
  tablePtr->createProc = BogusCreate;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_FirstHashEntry --
 *
 *	Locate the first entry in a hash table and set up a record
 *	that can be used to step through all the remaining entries
 *	of the table.
 *
 * Results:
 *	The return value is a pointer to the first entry in tablePtr,
 *	or NULL if tablePtr has no entries in it.  The memory at
 *	*searchPtr is initialized so that subsequent calls to
 *	Tcl_NextHashEntry will return all of the entries in the table,
 *	one at a time.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_HashEntry *
Tcl_FirstHashEntry(tablePtr, searchPtr)
     Tcl_HashTable *tablePtr;		/* Table to search. */
     Tcl_HashSearch *searchPtr;		/* Place to store information about
					 * progress through the table. */
{
  searchPtr->tablePtr = tablePtr;
  searchPtr->nextIndex = 0;
  searchPtr->nextEntryPtr = NULL;
  return Tcl_NextHashEntry(searchPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_NextHashEntry --
 *
 *	Once a hash table enumeration has been initiated by calling
 *	Tcl_FirstHashEntry, this procedure may be called to return
 *	successive elements of the table.
 *
 * Results:
 *	The return value is the next entry in the hash table being
 *	enumerated, or NULL if the end of the table is reached.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Tcl_HashEntry *
Tcl_NextHashEntry(searchPtr)
     register Tcl_HashSearch *searchPtr;	/* Place to store information about
                                                 * progress through the table.  Must
                                                 * have been initialized by calling
                                                 * Tcl_FirstHashEntry. */
{
  Tcl_HashEntry *hPtr;

  while (searchPtr->nextEntryPtr == NULL) {
    if (searchPtr->nextIndex >= searchPtr->tablePtr->numBuckets) {
      return NULL;
    }
    searchPtr->nextEntryPtr =
      searchPtr->tablePtr->buckets[searchPtr->nextIndex];
    searchPtr->nextIndex++;
  }
  hPtr = searchPtr->nextEntryPtr;
  searchPtr->nextEntryPtr = hPtr->nextPtr;
  return hPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_HashStats --
 *
 *	Return statistics describing the layout of the hash table
 *	in its hash buckets.
 *
 * Results:
 *	The return value is a malloc-ed string containing information
 *	about tablePtr.  It is the caller's responsibility to free
 *	this string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Tcl_HashStats(tablePtr)
     Tcl_HashTable *tablePtr;		/* Table for which to produce stats. */
{
#define NUM_COUNTERS 10
  int count[NUM_COUNTERS], overflow, i, j;
  double average, tmp;
  register Tcl_HashEntry *hPtr;
  char *result, *p;

  /*
   * Compute a histogram of bucket usage.
   */

  for (i = 0; i < NUM_COUNTERS; i++) {
    count[i] = 0;
  }
  overflow = 0;
  average = 0.0;
  for (i = 0; i < tablePtr->numBuckets; i++) {
    j = 0;
    for (hPtr = tablePtr->buckets[i]; hPtr != NULL; hPtr = hPtr->nextPtr) {
      j++;
    }
    if (j < NUM_COUNTERS) {
      count[j]++;
    } else {
      overflow++;
    }
    tmp = j;
    average += (tmp+1.0)*(tmp/tablePtr->numEntries)/2.0;
  }

  /*
   * Print out the histogram and a few other pieces of information.
   */

  result = (char *) ckalloc((unsigned) ((NUM_COUNTERS*60) + 300));
  sprintf(result, "%d entries in table, %d buckets\n",
          tablePtr->numEntries, tablePtr->numBuckets);
  p = result + strlen(result);
  for (i = 0; i < NUM_COUNTERS; i++) {
    sprintf(p, "number of buckets with %d entries: %d\n",
            i, count[i]);
    p += strlen(p);
  }
  sprintf(p, "number of buckets with %d or more entries: %d\n",
          NUM_COUNTERS, overflow);
  p += strlen(p);
  sprintf(p, "average search distance for entry: %.1f", average);
  return result;
}

/*
 *----------------------------------------------------------------------
 *
 * HashString --
 *
 *	Compute a one-word summary of a text string, which can be
 *	used to generate a hash index.
 *
 * Results:
 *	The return value is a one-word summary of the information in
 *	string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static unsigned int
HashString(string)
     register CONST char *string;/* String from which to compute hash value. */
{
  register unsigned int result;
  register int c;

  /*
   * I tried a zillion different hash functions and asked many other
   * people for advice.  Many people had their own favorite functions,
   * all different, but no-one had much idea why they were good ones.
   * I chose the one below (multiply by 9 and add new character)
   * because of the following reasons:
   *
   * 1. Multiplying by 10 is perfect for keys that are decimal strings,
   *    and multiplying by 9 is just about as good.
   * 2. Times-9 is (shift-left-3) plus (old).  This means that each
   *    character's bits hang around in the low-order bits of the
   *    hash value for ever, plus they spread fairly rapidly up to
   *    the high-order bits to fill out the hash value.  This seems
   *    works well both for decimal and non-decimal strings.
   */

  result = 0;
  while (1) {
    c = *string;
    string++;
    if (c == 0) {
      break;
    }
    result += (result<<3) + c;
  }
  return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringFind --
 *
 *	Given a hash table with string keys, and a string key, find
 *	the entry with a matching key.
 *
 * Results:
 *	The return value is a token for the matching entry in the
 *	hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
StringFind(tablePtr, key)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     CONST char *key;		/* Key to use to find matching entry. */
{
  register Tcl_HashEntry *hPtr;
  register CONST char *p1, *p2;
  int index;

  index = HashString(key) & tablePtr->mask;

  /*
   * Search all of the entries in the appropriate bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    for (p1 = key, p2 = hPtr->key.string; ; p1++, p2++) {
      if (*p1 != *p2) {
        break;
      }
      if (*p1 == '\0') {
        return hPtr;
      }
    }
  }
  return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * StringCreate --
 *
 *	Given a hash table with string keys, and a string key, find
 *	the entry with a matching key.  If there is no matching entry,
 *	then create a new entry that does match.
 *
 * Results:
 *	The return value is a pointer to the matching entry.  If this
 *	is a newly-created entry, then *newPtr will be set to a non-zero
 *	value;  otherwise *newPtr will be set to 0.  If this is a new
 *	entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *	A new entry may be added to the hash table.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
StringCreate(tablePtr, key, newPtr)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     CONST char *key;		/* Key to use to find or create matching
				 * entry. */
     int *newPtr;		/* Store info here telling whether a new
				 * entry was created. */
{
  register Tcl_HashEntry *hPtr;
  register CONST char *p1, *p2;
  int index;

  index = HashString(key) & tablePtr->mask;

  /*
   * Search all of the entries in this bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    for (p1 = key, p2 = hPtr->key.string; ; p1++, p2++) {
      if (*p1 != *p2) {
        break;
      }
      if (*p1 == '\0') {
        *newPtr = 0;
        return hPtr;
      }
    }
  }

  /*
   * Entry not found.  Add a new one to the bucket.
   */

  *newPtr = 1;
  hPtr = (Tcl_HashEntry *) ckalloc((unsigned)
                                   (sizeof(Tcl_HashEntry) + strlen(key) - (sizeof(hPtr->key) -1)));
  hPtr->tablePtr = tablePtr;
  hPtr->bucketPtr = &(tablePtr->buckets[index]);
  hPtr->nextPtr = *hPtr->bucketPtr;
  hPtr->clientData = 0;
  strcpy(hPtr->key.string, key);
  *hPtr->bucketPtr = hPtr;
  tablePtr->numEntries++;

  /*
   * If the table has exceeded a decent size, rebuild it with many
   * more buckets.
   */

  if (tablePtr->numEntries >= tablePtr->rebuildSize) {
    RebuildTable(tablePtr);
  }
  return hPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * OneWordFind --
 *
 *	Given a hash table with one-word keys, and a one-word key, find
 *	the entry with a matching key.
 *
 * Results:
 *	The return value is a token for the matching entry in the
 *	hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
OneWordFind(tablePtr, key)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     register CONST char *key;	/* Key to use to find matching entry. */
{
  register Tcl_HashEntry *hPtr;
  int index;

  index = RANDOM_INDEX(tablePtr, key);

  /*
   * Search all of the entries in the appropriate bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    if (hPtr->key.oneWordValue == key) {
      return hPtr;
    }
  }
  return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * OneWordCreate --
 *
 *	Given a hash table with one-word keys, and a one-word key, find
 *	the entry with a matching key.  If there is no matching entry,
 *	then create a new entry that does match.
 *
 * Results:
 *	The return value is a pointer to the matching entry.  If this
 *	is a newly-created entry, then *newPtr will be set to a non-zero
 *	value;  otherwise *newPtr will be set to 0.  If this is a new
 *	entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *	A new entry may be added to the hash table.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
OneWordCreate(tablePtr, key, newPtr)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     register CONST char *key;	/* Key to use to find or create matching
				 * entry. */
     int *newPtr;		/* Store info here telling whether a new
				 * entry was created. */
{
  register Tcl_HashEntry *hPtr;
  int index;

  index = RANDOM_INDEX(tablePtr, key);

  /*
   * Search all of the entries in this bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    if (hPtr->key.oneWordValue == key) {
      *newPtr = 0;
      return hPtr;
    }
  }

  /*
   * Entry not found.  Add a new one to the bucket.
   */

  *newPtr = 1;
  hPtr = (Tcl_HashEntry *) ckalloc(sizeof(Tcl_HashEntry));
  hPtr->tablePtr = tablePtr;
  hPtr->bucketPtr = &(tablePtr->buckets[index]);
  hPtr->nextPtr = *hPtr->bucketPtr;
  hPtr->clientData = 0;
  hPtr->key.oneWordValue = (char *) key;	/* CONST XXXX */
  *hPtr->bucketPtr = hPtr;
  tablePtr->numEntries++;

  /*
   * If the table has exceeded a decent size, rebuild it with many
   * more buckets.
   */

  if (tablePtr->numEntries >= tablePtr->rebuildSize) {
    RebuildTable(tablePtr);
  }
  return hPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ArrayFind --
 *
 *	Given a hash table with array-of-int keys, and a key, find
 *	the entry with a matching key.
 *
 * Results:
 *	The return value is a token for the matching entry in the
 *	hash table, or NULL if there was no matching entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
ArrayFind(tablePtr, key)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     CONST char *key;		/* Key to use to find matching entry. */
{
  register Tcl_HashEntry *hPtr;
  int *arrayPtr = (int *) key;
  register int *iPtr1, *iPtr2;
  int index, count;

  for (index = 0, count = tablePtr->keyType, iPtr1 = arrayPtr;
       count > 0; count--, iPtr1++) {
    index += *iPtr1;
  }
  index = RANDOM_INDEX(tablePtr, index);

  /*
   * Search all of the entries in the appropriate bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    for (iPtr1 = arrayPtr, iPtr2 = hPtr->key.words,
           count = tablePtr->keyType; ; count--, iPtr1++, iPtr2++) {
      if (count == 0) {
        return hPtr;
      }
      if (*iPtr1 != *iPtr2) {
        break;
      }
    }
  }
  return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ArrayCreate --
 *
 *	Given a hash table with one-word keys, and a one-word key, find
 *	the entry with a matching key.  If there is no matching entry,
 *	then create a new entry that does match.
 *
 * Results:
 *	The return value is a pointer to the matching entry.  If this
 *	is a newly-created entry, then *newPtr will be set to a non-zero
 *	value;  otherwise *newPtr will be set to 0.  If this is a new
 *	entry the value stored in the entry will initially be 0.
 *
 * Side effects:
 *	A new entry may be added to the hash table.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
ArrayCreate(tablePtr, key, newPtr)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     register CONST char *key;	/* Key to use to find or create matching
				 * entry. */
     int *newPtr;		/* Store info here telling whether a new
				 * entry was created. */
{
  register Tcl_HashEntry *hPtr;
  int *arrayPtr = (int *) key;
  register int *iPtr1, *iPtr2;
  int index, count;

  for (index = 0, count = tablePtr->keyType, iPtr1 = arrayPtr;
       count > 0; count--, iPtr1++) {
    index += *iPtr1;
  }
  index = RANDOM_INDEX(tablePtr, index);

  /*
   * Search all of the entries in the appropriate bucket.
   */

  for (hPtr = tablePtr->buckets[index]; hPtr != NULL;
       hPtr = hPtr->nextPtr) {
    for (iPtr1 = arrayPtr, iPtr2 = hPtr->key.words,
           count = tablePtr->keyType; ; count--, iPtr1++, iPtr2++) {
      if (count == 0) {
        *newPtr = 0;
        return hPtr;
      }
      if (*iPtr1 != *iPtr2) {
        break;
      }
    }
  }

  /*
   * Entry not found.  Add a new one to the bucket.
   */

  *newPtr = 1;
  hPtr = (Tcl_HashEntry *) ckalloc((unsigned) (sizeof(Tcl_HashEntry)
                                               + (tablePtr->keyType*sizeof(int)) - 4));
  hPtr->tablePtr = tablePtr;
  hPtr->bucketPtr = &(tablePtr->buckets[index]);
  hPtr->nextPtr = *hPtr->bucketPtr;
  hPtr->clientData = 0;
  for (iPtr1 = arrayPtr, iPtr2 = hPtr->key.words, count = tablePtr->keyType;
       count > 0; count--, iPtr1++, iPtr2++) {
    *iPtr2 = *iPtr1;
  }
  *hPtr->bucketPtr = hPtr;
  tablePtr->numEntries++;

  /*
   * If the table has exceeded a decent size, rebuild it with many
   * more buckets.
   */

  if (tablePtr->numEntries >= tablePtr->rebuildSize) {
    RebuildTable(tablePtr);
  }
  return hPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * BogusFind --
 *
 *	This procedure is invoked when an Tcl_FindHashEntry is called
 *	on a table that has been deleted.
 *
 * Results:
 *	If panic returns (which it shouldn't) this procedure returns
 *	NULL.
 *
 * Side effects:
 *	Generates a panic.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static Tcl_HashEntry *
BogusFind(tablePtr, key)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     CONST char *key;		/* Key to use to find matching entry. */
{
  panic("called Tcl_FindHashEntry on deleted table");
  return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * BogusCreate --
 *
 *	This procedure is invoked when an Tcl_CreateHashEntry is called
 *	on a table that has been deleted.
 *
 * Results:
 *	If panic returns (which it shouldn't) this procedure returns
 *	NULL.
 *
 * Side effects:
 *	Generates a panic.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static Tcl_HashEntry *
BogusCreate(tablePtr, key, newPtr)
     Tcl_HashTable *tablePtr;	/* Table in which to lookup entry. */
     CONST char *key;		/* Key to use to find or create matching
				 * entry. */
     int *newPtr;		/* Store info here telling whether a new
				 * entry was created. */
{
  panic("called Tcl_CreateHashEntry on deleted table");
  return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * RebuildTable --
 *
 *	This procedure is invoked when the ratio of entries to hash
 *	buckets becomes too large.  It creates a new table with a
 *	larger bucket array and moves all of the entries into the
 *	new table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory gets reallocated and entries get re-hashed to new
 *	buckets.
 *
 *----------------------------------------------------------------------
 */

static void
RebuildTable(tablePtr)
     register Tcl_HashTable *tablePtr;	/* Table to enlarge. */
{
  int oldSize, count, index;
  Tcl_HashEntry **oldBuckets;
  register Tcl_HashEntry **oldChainPtr, **newChainPtr;
  register Tcl_HashEntry *hPtr;

  oldSize = tablePtr->numBuckets;
  oldBuckets = tablePtr->buckets;

  /*
   * Allocate and initialize the new bucket array, and set up
   * hashing constants for new array size.
   */

  tablePtr->numBuckets *= 4;
  tablePtr->buckets = (Tcl_HashEntry **) ckalloc((unsigned)
                                                 (tablePtr->numBuckets * sizeof(Tcl_HashEntry *)));
  for (count = tablePtr->numBuckets, newChainPtr = tablePtr->buckets;
       count > 0; count--, newChainPtr++) {
    *newChainPtr = NULL;
  }
  tablePtr->rebuildSize *= 4;
  tablePtr->downShift -= 2;
  tablePtr->mask = (tablePtr->mask << 2) + 3;

  /*
   * Rehash all of the existing entries into the new bucket array.
   */

  for (oldChainPtr = oldBuckets; oldSize > 0; oldSize--, oldChainPtr++) {
    for (hPtr = *oldChainPtr; hPtr != NULL; hPtr = *oldChainPtr) {
      *oldChainPtr = hPtr->nextPtr;
      if (tablePtr->keyType == TCL_STRING_KEYS) {
        index = HashString(hPtr->key.string) & tablePtr->mask;
      } else if (tablePtr->keyType == TCL_ONE_WORD_KEYS) {
        index = RANDOM_INDEX(tablePtr, hPtr->key.oneWordValue);
      } else {
        register int *iPtr;
        int count;

        for (index = 0, count = tablePtr->keyType,
               iPtr = hPtr->key.words; count > 0; count--, iPtr++) {
          index += *iPtr;
        }
        index = RANDOM_INDEX(tablePtr, index);
      }
      hPtr->bucketPtr = &(tablePtr->buckets[index]);
      hPtr->nextPtr = *hPtr->bucketPtr;
      *hPtr->bucketPtr = hPtr;
    }
  }

  /*
   * Free up the old bucket array, if it was dynamically allocated.
   */

  if (oldBuckets != tablePtr->staticBuckets) {
    ckfree((char *) oldBuckets);
  }
}

#endif
