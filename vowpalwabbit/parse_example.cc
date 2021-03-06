/*
Copyright (c) 2009 Yahoo! Inc.  All rights reserved.  The copyrights
embodied in the content of this file are licensed under the BSD
(revised) open source license
 */

#include <math.h>
#include <ctype.h>
#include "parse_example.h"
#include "hash.h"
#include "cache.h"
#include "unique_sort.h"
#include "global_data.h"

using namespace std;

size_t hashstring (substring s, unsigned long h)
{
  size_t ret = 0;
  //trim leading whitespace but not UTF-8
  for(; s.begin < s.end && *(s.begin) <= 0x20 && (int)*(s.begin) >= 0; s.begin++);
  //trim trailing white space but not UTF-8
  for(; s.end > s.begin && *(s.end-1) <= 0x20 && (int)*(s.end-1) >=0; s.end--);

  char *p = s.begin;
  while (p != s.end)
    if (isdigit(*p))
      ret = 10*ret + *(p++) - '0';
    else
      return uniform_hash((unsigned char *)s.begin, s.end - s.begin, h);

  return ret + h;
}

size_t hashall (substring s, unsigned long h)
{
  return uniform_hash((unsigned char *)s.begin, s.end - s.begin, h);
}

hash_func_t getHasher(const string& s){
  if (s=="strings")
    return hashstring;
  else if(s=="all")
    return hashall;
  else{
    cerr << "Unknown hash function: " << s << ". Exiting " << endl;
    exit(1);
  }
}

void feature_value(substring &s, v_array<substring>& name, float &v)
{
  tokenize(':', s, name);
  
  switch (name.index()) {
  case 0:
  case 1:
    v = 1.;
    break;
  case 2:
    v = float_of_substring(name[1]);
    if ( isnan(v))
      {
	cerr << "error NaN value for feature: ";
	cerr.write(name[0].begin, name[0].end - name[0].begin);
	cerr << " terminating." << endl;
	exit(1);
      }
    break;
  default:
    cerr << "example with a wierd name.  What is ";
    cerr.write(s.begin, s.end - s.begin);
    cerr << "\n";
  }
}

// new should be empty. --Alex
template<class T> void copy_v_array(v_array<T>& old, v_array<T>& new_va)
{
  // allocate new memory for new.
  new_va.begin = (T *) malloc(sizeof(T)*old.index());

  // copy the old to the new.
  memcpy(new_va.begin,old.begin,sizeof(T)*old.index());
  
  // update pointers
  new_va.end = new_va.begin + old.index();
  new_va.end_array = new_va.end;
}

char* c_string_of_substring(substring s)
{
  size_t len = s.end - s.begin+1;
  char* ret = (char *)calloc(len,sizeof(char));
  memcpy(ret,s.begin,len-1);
  return ret;
}

char* copy(char* base)
{
  size_t len = 0;
  while (base[len++] != '\0');
  char* ret = (char *)calloc(len,sizeof(char));
  memcpy(ret,base,len);
  return ret;
}

void substring_to_example(vw* all, example* ae, substring example)
{
  tokenize('|', example, all->p->channels);
  all->p->lp->default_label(ae->ld);
  substring* feature_start = &(all->p->channels[1]);

  substring label_space = all->p->channels[0];
  if (*example.begin == '|')
    {
      feature_start = &(all->p->channels[0]);
      all->p->words.erase();
    }
  else
    {
      char* tab_location = safe_index(label_space.begin, '\t', label_space.end);
      if (tab_location != label_space.end)
	label_space.begin = tab_location+1;
      
      tokenize(' ',label_space,all->p->words);
      if (all->p->words.index() > 0 && (all->p->words.last().end == label_space.end || *(all->p->words.last().begin)=='\'') ) //The last field is a tag, so record and strip it off
	{
	  substring tag = all->p->words.pop();
	  if (*tag.begin == '\'')
	    tag.begin++;
	  
	  push_many(ae->tag, tag.begin, tag.end - tag.begin);
	}
    }
  all->p->lp->parse_label(all->sd, ae->ld, all->p->words);

  size_t mask = all->parse_mask;
  bool audit = all->audit;
  for (substring* i = feature_start; i != all->p->channels.end; i++) {
    substring channel = *i;
    
    tokenize(' ',channel, all->p->words);
    if (all->p->words.begin == all->p->words.end)
      continue;
    
    float channel_v = 1.;
    size_t channel_hash;
    char* base=NULL;
    size_t index = 0;
    bool new_index = false;
    size_t feature_offset = 0;
    if (channel.begin[0] != ' ')//we have a nonanonymous namespace
      {
	feature_offset++;
	feature_value(all->p->words[0], all->p->name, channel_v);

	if (all->p->name.index() > 0) {
	  index = (unsigned char)(*all->p->name[0].begin);
	  if (ae->atomics[index].begin == ae->atomics[index].end)
	    new_index = true;
	}
	if (audit)
	  base = c_string_of_substring(all->p->name[0]);
	channel_hash = all->p->hasher(all->p->name[0], hash_base);
      }
    else
      {
	index = (unsigned char)' ';
	if (ae->atomics[index].begin == ae->atomics[index].end)
	  new_index = true;
	if (audit)
	  {
	    base = (char *)calloc(2,sizeof(char));
	    base[0]=' ';
	    base[1]='\0';
	  }
	channel_hash = 0;
      }
 
    for (substring* i = all->p->words.begin+feature_offset; i != all->p->words.end; i++) {
      float v = 0.;
      feature_value(*i, all->p->name, v);
      v *= channel_v;

      size_t word_hash = (all->p->hasher(all->p->name[0], channel_hash)) & mask;
      feature f = {v,(uint32_t)word_hash};
      ae->sum_feat_sq[index] += v*v;
      push(ae->atomics[index], f);
    }

    if (new_index && ae->atomics[index].begin != ae->atomics[index].end)
      push(ae->indices, index);
    
    if (audit)
      {
	for (substring* i = all->p->words.begin+feature_offset; i != all->p->words.end; i++) {
	  float v = 0.;
	  feature_value(*i, all->p->name, v);
	  v *= channel_v;

	  size_t word_hash = (all->p->hasher(all->p->name[0], channel_hash)) & mask;
      
	  char* feature = c_string_of_substring(all->p->name[0]);
	  audit_data ad = {copy(base), feature, word_hash, v, true};
	  push(ae->audit_features[index], ad);
	}
	free(base);
      }
  }

}

int read_features(void* in, example* ex)
{
  vw* all = (vw*)in;
  example* ae = (example*)ex;
  char *line=NULL;
  int num_chars = readto(*(all->p->input), line, '\n');
  if (num_chars <= 1)
    return num_chars;
  if (line[num_chars-1] == '\n')
    num_chars--;
  substring example = {line, line + num_chars};
  substring_to_example(all, ae, example);

  return num_chars;
}

void read_line(vw& all, example* ex, char* line)
{
  substring ss = {line, line+strlen(line)};
  substring_to_example(&all, ex, ss);  
}
