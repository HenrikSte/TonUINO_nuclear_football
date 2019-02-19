#ifndef _StringArray_h_
#define _StringArray_h_

#include <Arduino.h>
#include <fs.h>

class StringArray
{
  static const int MAX_STRINGS = 300;
protected:
  int count =0;
  void sort(String strings[], int count);
//helper:
  void swap(String& a, String& b);
  int  partition(String a[],int s,int e);
  void quickSort(String a[],int s,int e);

  String strings[MAX_STRINGS];

public:

  const String& operator[](std::size_t idx) const { return strings[idx]; }

  inline int getCount() {return count;};
  inline void sort() {quickSort(strings, 0, count-1);};

  bool add(const String& string);
  bool save(fs::File* file);
  bool load(fs::File* file);
  const String& getItem(int idx) { return strings[idx]; }
  int  getItemUTF8(const String& search);
  void free();

  static String convertToUTF8(const String& s);
  static String convertToHTML(const String& s);

};

#endif