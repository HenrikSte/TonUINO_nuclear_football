#include <arduino.h>

#define MAXCARDS 200

class Card
{
  public: 
    Card()
    {
      isDeleted=false;
    };
    String ID;
    String track;
    bool isDeleted;
};

class CardData
{
  public:
    int getCardCount()
    {
      return cardCount; // includes deleted cards!
    }

    
    int addCard(const char* ID, const char* track)
    {
      if (cardCount >= MAXCARDS)
      {
        return -1;
      }
      card[cardCount].ID = ID;
      card[cardCount].track = track;

      Serial.print("Adding Card[");
      Serial.print(cardCount);
      Serial.print("]: ");
      Serial.print(ID);
      Serial.print(":");
      Serial.println(track);

      cardCount++;
      return cardCount;
    }

    int addCard(const String& ID, const String& track)
    {
      return addCard(ID.c_str(), track.c_str());
    }

    Card& getCard(int i)
    {
      if (i<MAXCARDS)
      {
        return card[i];
      }
      else
      {
        return emptyCard;
      }
    }

    Card& getCard(const String& ID)
    {
      for (int i=0;i<cardCount; i++)
      {
        if (!card[i].isDeleted && card[i].ID.equals(ID))
        {
          return card[i];
        }
      }
      return emptyCard;
    }

    const Card& getCardByTrack(const String& track)
    {
      for (int i=0;i<cardCount; i++)
      {
        if (!card[i].isDeleted && card[i].track.equals(track))
        {
          return card[i];
        }
      }
      return emptyCard;
    }

    int cardExists(const String& cardId)
    {
      return getCard(cardId).ID.length()!=0;
    }



  protected:
    Card card[MAXCARDS];
    int cardCount=0;
    Card emptyCard;

};

CardData cardData;