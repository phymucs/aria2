/* <!-- copyright */
/*
 * aria2 - a simple utility for downloading files faster
 *
 * Copyright (C) 2006 Tatsuhiro Tsujikawa
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
 */
/* copyright --> */
#include "MultiDiskWriter.h"
#include "DlAbortEx.h"
#include "Util.h"
#include <errno.h>

MultiDiskWriter::MultiDiskWriter() {
#ifdef ENABLE_SHA1DIGEST
  sha1DigestInit(ctx);
#endif // ENABLE_SHA1DIGEST
}

MultiDiskWriter::~MultiDiskWriter() {
  clearEntries();
#ifdef ENABLE_SHA1DIGEST
  sha1DigestFree(ctx);
#endif // ENABLE_SHA1DIGEST
}

typedef struct {
  long long int blockOffset;
  long long int length;
} Range;

typedef deque<Range> Ranges;

void MultiDiskWriter::clearEntries() {
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end(); itr++) {
    if((*itr)->enabled) {
      (*itr)->diskWriter->closeFile();
    }
    delete *itr;
  }
  diskWriterEntries.clear();
}

void MultiDiskWriter::setMultiFileEntries(const MultiFileEntries& multiFileEntries, int pieceLength) {
  clearEntries();
  Ranges ranges;
  for(MultiFileEntries::const_iterator itr = multiFileEntries.begin();
      itr != multiFileEntries.end(); itr++) {
    if(itr->requested) {
      Range range;
      range.blockOffset = (itr->offset/pieceLength)*pieceLength;
      range.length = ((itr->offset+itr->length)/pieceLength+
	((itr->offset+itr->length)%pieceLength ? 1 : 0))*pieceLength-range.blockOffset;
      ranges.push_back(range);
    }		
  }
  Ranges::const_iterator ritr = ranges.begin();
  for(MultiFileEntries::const_iterator itr = multiFileEntries.begin();
      itr != multiFileEntries.end(); itr++) {
    DiskWriterEntry* entry;
    if(ritr != ranges.end() &&
       //       !(ritr->blockOffset+ritr->length-1 < itr->offset ||
       //	 itr->offset+itr->length-1 < ritr->blockOffset)) {
       itr->offset < ritr->blockOffset+ritr->length &&
       ritr->blockOffset < itr->offset+itr->length) {

      entry = new DiskWriterEntry(*itr, true);
      for(;ritr->blockOffset+ritr->length <= itr->offset+itr->length &&
	    ritr != ranges.end(); ritr++);
    } else {
      entry = new DiskWriterEntry(*itr, false);
    }
    diskWriterEntries.push_back(entry);
  }
}

void MultiDiskWriter::openFile(const string& filename) {
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end(); itr++) {
    if((*itr)->enabled) {
      (*itr)->diskWriter->openFile(filename+"/"+(*itr)->fileEntry.path);
    }
  }
}

// filename is a directory which is specified by the user in the option.
void MultiDiskWriter::initAndOpenFile(string filename) {
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end(); itr++) {
    if((*itr)->enabled) {
      (*itr)->diskWriter->initAndOpenFile(filename+"/"+(*itr)->fileEntry.path);
    }
  }
}

void MultiDiskWriter::closeFile() {
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end(); itr++) {
    if((*itr)->enabled) {
      (*itr)->diskWriter->closeFile();
    }
  }
}

void MultiDiskWriter::openExistingFile(string filename) {
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end(); itr++) {
    if((*itr)->enabled) {
      (*itr)->diskWriter->openExistingFile(filename+"/"+(*itr)->fileEntry.path);
    }
  }
}

void MultiDiskWriter::writeData(const char* data, int len, long long int position) {
  long long int offset = position;
  long long int fileOffset = offset;
  bool writing = false;
  int rem = len;
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end() && rem != 0; itr++) {
    if(isInRange(*itr, offset) || writing) {
      if(!(*itr)->enabled) {
	throw new DlAbortEx("invalid offset or length. offset = %lld", offset);
      }
      int writeLength = calculateLength(*itr, fileOffset, rem);
      (*itr)->diskWriter->writeData(data+(len-rem), writeLength, fileOffset);
      rem -= writeLength;
      writing = true;
      fileOffset = 0;
    } else {
      fileOffset -= (*itr)->fileEntry.length;
    }
  }
  if(!writing) {
    throw new DlAbortEx("offset out of range");
  }
}

bool MultiDiskWriter::isInRange(const DiskWriterEntry* entry, long long int offset) const {
  return entry->fileEntry.offset <= offset &&
    offset < entry->fileEntry.offset+entry->fileEntry.length;
}

int MultiDiskWriter::calculateLength(const DiskWriterEntry* entry, long long int fileOffset, int rem) const {
  int length;
  if(entry->fileEntry.length < fileOffset+rem) {
    length = entry->fileEntry.length-fileOffset;
  } else {
    length = rem;
  }
  return length;
}

int MultiDiskWriter::readData(char* data, int len, long long int position) {
  long long int offset = position;
  long long int fileOffset = offset;
  bool reading = false;
  int rem = len;
  int totalReadLength = 0;
  for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
      itr != diskWriterEntries.end() && rem != 0; itr++) {
    if(isInRange(*itr, offset) || reading) {
      if(!(*itr)->enabled) {
	throw new DlAbortEx("invalid offset or length. offset = %lld", offset);
      }
      int readLength = calculateLength((*itr), fileOffset, rem);
      totalReadLength += (*itr)->diskWriter->readData(data+(len-rem), readLength, fileOffset);
      rem -= readLength;
      reading = true;
      fileOffset = 0;
    } else {
      fileOffset -= (*itr)->fileEntry.length;
    }
  }
  if(!reading) {
    throw new DlAbortEx("offset out of range");
  }
  return totalReadLength;
}

#ifdef ENABLE_SHA1DIGEST
void MultiDiskWriter::hashUpdate(const DiskWriterEntry* entry, long long int offset, long long int length) const {
  int BUFSIZE = 16*1024;
  char buf[BUFSIZE];
  for(int i = 0; i < length/BUFSIZE; i++) {
    if(BUFSIZE != entry->diskWriter->readData(buf, BUFSIZE, offset)) {
      throw "error";
    }
    sha1DigestUpdate(ctx, buf, BUFSIZE);
    offset += BUFSIZE;
  }
  int r = length%BUFSIZE;
  if(r > 0) {
    if(r != entry->diskWriter->readData(buf, r, offset)) {
      throw "error";
    }
    sha1DigestUpdate(ctx, buf, r);
  }
}
#endif // ENABLE_SHA1DIGEST

string MultiDiskWriter::sha1Sum(long long int offset, long long int length) {
#ifdef ENABLE_SHA1DIGEST
  long long int fileOffset = offset;
  bool reading = false;
  int rem = length;
  sha1DigestReset(ctx);
  try {
    for(DiskWriterEntries::iterator itr = diskWriterEntries.begin();
	itr != diskWriterEntries.end() && rem != 0; itr++) {
      if(isInRange(*itr, offset) || reading) {
	if(!(*itr)->enabled) {
	  throw new DlAbortEx("invalid offset or length. offset = %lld", offset);
	}
	int readLength = calculateLength((*itr), fileOffset, rem);
	hashUpdate(*itr, fileOffset, readLength);
	rem -= readLength;
	reading = true;
	fileOffset = 0;
      } else {
	fileOffset -= (*itr)->fileEntry.length;
      }
    }
    if(!reading) {
      throw new DlAbortEx("offset out of range");
    }
    unsigned char hashValue[20];
    sha1DigestFinal(ctx, hashValue);
    return Util::toHex(hashValue, 20);
  } catch(string ex) {
    throw new DlAbortEx(strerror(errno));
  }
#else
  return "";
#endif // ENABLE_SHA1DIGEST
}

