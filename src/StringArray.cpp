#include "StringArray.h"

  bool StringArray::add(const String& string)
  {
    if (count<MAX_STRINGS)
    {
      strings[count] = string;
      count++;
      return true;
    }
    else
    {
      ESP_LOGD(TAG, "StringArray full (%d)", count);
      return false;
    }
  }

  void StringArray::free()
  {
    for (int i=0; i<count; i++)
    {
      strings[i]="";
    }
    count=0;
  }

const int bufSize = 1500;
unsigned char buffer[bufSize];

  bool StringArray::save(fs::File* file)
  {
    if (file)
    {
      for (int x=0; x<count; x++)
      {
        //file->println(strings[i]);
        buffer[0]=0;
        strings[x].getBytes(buffer,bufSize,0); 
      
        int bi=0;
        while (buffer[bi])
        {
          file->write(buffer[bi]);
          bi++;
        }
        file->println();
      }
      return true;
    }
    else
    {
      return false;
    }

  }

  bool StringArray::load(fs::File* file)
  {
    if (file)
    { 
      String line;
      do
      {
        line = file->readStringUntil('\n');
        line.trim();
        add(line);
      } while (line.length());
      
      return true;
    }
    else
    {
      return false;
    }

  }


  void StringArray::swap(String& a, String& b)
  {
    String t=a;
    a=b;
    b=t;
  }


  int  StringArray::getItemUTF8(const String& search)
  {
    for (int i=0; i<count; i++)
    {
      String utf8 = convertToUTF8(strings[i]);
      if (utf8.equalsIgnoreCase(search))
      {
        return i;
      }
    }
    return -1;
  }


  int StringArray::partition(String a[],int s,int e)
  {
      const String& piviot=a[e];
      int pind=s;
      int i;
      for(i=s;i<e;i++)
      {
        if(a[i]<=piviot)
          {
            swap(a[i],a[pind]);
              pind++;
          }
      }

      swap(a[e],a[pind]);
      return pind;
  }

  void StringArray::quickSort(String a[],int s,int e)
  {
    if(s<e)
    {
      int pind=partition(a,s,e);
          quickSort(a,s,pind-1);
          quickSort(a,pind+1,e);
    }
  }


String StringArray::convertToUTF8(const String& s)
{
  int len = s.length();
  bool found = false;

  for (int i=0; i<len; i++)
  {
    if (s[i]> 127)
    {
      found= true;
      break;
    }
  }

  if (found)
  {
    /*
    int i=0;
    while (s[i])
    {
      Serial.print((int)i);
      Serial.print(":");
      Serial.print((int)s[i]);
      Serial.print(":");
      Serial.println(s[i]);  
      i++;
    }
    */
    String str;
    for (int i=0; i<len; i++)
    {
      if (s[i]> 127)
      {
        String c = "_";
        switch (s[i])
        {
          case 129:
            c="ü";
          break;
          case 132:
            c="ä";
          break;
          case 142:
            c="Ä";
          break;
          case 148:
            c="ö";
          break;
          case 153:
            c="Ö";
          break;
          case 154:
            c="Ü";
          break;
          case 225:
            c="ß";
          break;
          default:
          break;
        }
        str += c;
      }
      else
      {
        str += s[i];
      }
    }
    /*
    Serial.print("converted String: ");
    Serial.println(str);
    */
    return str;
  }
  else
  {
    return s;
  }
  
}

 String StringArray::convertToHTML(const String& s)
{
  String str = s;
  str.replace("&", "&amp;"); // must be first :-)
  str.replace("ü", "&uuml;");
  str.replace("ä", "&auml;");
  str.replace("ö", "&ouml;");
  str.replace("Ü", "&Uuml;");
  str.replace("Ä", "&Auml;");
  str.replace("Ö", "&Öuml;");
  str.replace("ß", "&szlig;");
  str.replace("€", "&euro;");
  str.replace("<", "&lt;");
  str.replace(">", "&gt;");

/*
  Serial.print("converted String: ");
  Serial.println(str);
*/
  return str;
} 

void StringArray::print()
{
  for (int i=0; i<count; i++)
  {
    Serial.println(strings[i]);
  }
}

const bool StringArray::hasItem(const String& string)
{
  if (string.length())
  {
    for (int i=0; i<count; i++)
    {
      if (strings[i].length() && strings[i].equalsIgnoreCase(string))
      {
        return true;
      }
    }
  }
  return false;
} 
