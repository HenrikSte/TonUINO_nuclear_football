
#include "myautoconnect.h"

#if defined(ARDUINO_ARCH_ESP8266)
ESP8266WebServer acServer;
#elif defined(ARDUINO_ARCH_ESP32)
WebServer acServer;
#endif

AutoConnect       Portal(acServer);
AutoConnectAux    Ip;
AutoConnectConfig Config;       

const char  configFileName[]  PROGMEM = "/config.json";

static const char  content[]  PROGMEM = R"(
  <html>
    <head>
      <meta http-equiv="refresh" content="3; URL=/_ac/config">
    </head>
    <body>
      <h1>Forwarding to auto connect portal</h1>
      <p>Otherwise <a href="_ac/config" >click here</a>!</p>
    </body>
  </html>
  )";

static const char AUX_IPCONFIG[] PROGMEM = R"(
{
  "title": "IP Config",
  "uri": "/ipconfig",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "Setting Ip adresses",
      "style": "font-family:Arial;font-weight:bold;text-align:center;margin-bottom:10px;color:DarkSlateBlue"
    },
    {
      "name": "IP",
      "type": "ACInput",
      "label": "Static IP",
      "placeholder": "192.168.150.x"
    },
        {
      "name": "GW",
      "type": "ACInput",
      "label": "Gateway",
      "placeholder": "192.168.150.1"
    },
    {
      "name": "SN",
      "type": "ACInput",
      "label": "Subnet Mask",
      "placeholder": "255.255.255.1"
    },
    {
      "name": "newline_",
      "type": "ACElement",
      "value": "<br>"
    },
    {
      "name": "start_",
      "type": "ACSubmit",
      "value": "OK",
      "uri": "/saveip"
    }
  ]
}
)";


void saveIpPage() {
  // Retrieve the value of AutoConnectElement with arg function of WebServer class.
  // Values are accessible with the element name.
  String  staticIp = acServer.arg("IP");
  String  staticGateway = acServer.arg("GW");
  String  staticSubnetMask = acServer.arg("SN");
  
  if (staticIp.length())
  {
    if (!staticGateway.length())
    {
      staticGateway = "192.168.150.1";
    }

    if (!staticSubnetMask.length())
    {
      staticSubnetMask = "255.255.255.0";
    }

    Serial.print("saveIpPage, IP=");
    Serial.print(staticIp);
    Serial.print(", GW=");
    Serial.print(staticGateway);
    Serial.print(", SN=");
    Serial.println(staticSubnetMask);

    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    // JSONify local configuration parameters
    json["ip"] = staticIp;
    json["gw"] = staticGateway;
    json["sn"] = staticSubnetMask;


    Serial.print("Saving json file ");
    Serial.print (configFileName);

    File f = SPIFFS.open(configFileName, "w");

    if (f) 
    {
      json.prettyPrintTo(Serial);
      json.prettyPrintTo(f);
      f.close();

      Serial.println(" json file was successfully saved");
    }
    else
    {
      Serial.println(" Failed to open json file for writing");
    }
    IPAddress _ip,_gw,_sn;

    _ip.fromString(staticIp);
    _gw.fromString(staticGateway);
    _sn.fromString(staticSubnetMask); 
    
    Config.staip = _ip;      // Sets static IP
    Config.staGateway = _gw;  // Sets WiFi router address
    Config.staNetmask = _sn; // Sets WLAN scope
  }

  // The /start page just constitutes timezone,
  // it redirects to the root page without the content response.
  acServer.sendHeader("Location", String("http://") + acServer.client().localIP().toString() + String("/"));
  acServer.send(302, "text/plain", "");
  acServer.client().flush();
  acServer.client().stop();
}

void rootPage() 
{
   acServer.send(200, "text/html", content);
}

bool autoConnectWifi(AutoConnect::DetectExit_ft onDetect, bool forcePortal, const char * portalSSID)
{
  bool success = false;
 
  success = SPIFFS.begin();
  if (!success)
  {
    Serial.println("Failed to open SPIFFS, formatting...");
    success =  SPIFFS.format();
    if (!success)
    {
      Serial.println("Failed to format SPIFFS.");
    }
    else
    {
      success = SPIFFS.begin();
    }
  }
  

  if (success)
  {
    Serial.println("SPIFFS ok.");
  }


/*
  std::unique_ptr<AutoConnect> _Portal(new AutoConnect(acServer));
  std::unique_ptr<AutoConnectAux> _Ip(new AutoConnectAux);

  AutoConnect &  Portal = *_Portal.get();
  AutoConnectAux &  Ip  = *_Ip.get();
*/ 
  String staticIp;
  String staticGateway;
  String staticSubnetMask;

  File f = SPIFFS.open(configFileName, "r");
  if (!f || f.size()==0) 
  {
    
    Serial.print("Configuration file not found: ");
    Serial.println(configFileName);
  } 
  else 
  {
    size_t size = f.size();
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size]);

    f.readBytes(buf.get(), size);
    f.close();
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    if (!json.success()) 
    {
      Serial.println(configFileName);
      Serial.println("JSON parseObject() failed\n");
    }
    else
    {
      json.prettyPrintTo(Serial);

      if (json.containsKey("ip")) 
      {
        staticIp = (const char*)json["ip"];      
      }
      if (json.containsKey("gw")) 
      {
        staticGateway = (const char*)json["gw"];      
      }
      if (json.containsKey("sn")) 
      {
        staticSubnetMask = (const char*)json["sn"];      
      }
      Serial.println("\nConfig file was successfully parsed\n");
    }
    
  }

  IPAddress _ip,_gw,_sn;
  _ip.fromString(staticIp);
  _gw.fromString(staticGateway);
  _sn.fromString(staticSubnetMask); 
  
  Config.apip = IPAddress(192,168,4,1);      // Sets SoftAP IP address
  Config.gateway = IPAddress(192,168,4,1);     // Sets WLAN router IP address
  Config.netmask = IPAddress(255,255,255,0);    // Sets WLAN scope
  Config.autoReconnect = true;                  // Enable auto-reconnect
  //Config.portalTimeout = 30000;                 // Sets timeout value for the captive portal
  Config.retainPortal = true;                   // Retains the portal function after timed-out
  //Config.homeUri = "/index.html";               // Sets home path of the sketch application
  //Config.title ="My menu";                      // Customize the menu title
  Config.staip = _ip;      // Sets static IP
  Config.staGateway = _gw;  // Sets WiFi router address
  Config.staNetmask = _sn; // Sets WLAN scope
  //Config.dns1 = IPAddress(192,168,150,1);        // Sets primary DNS address
  Config.immediateStart = forcePortal;
//  Config.uptime = 10000;
  if (portalSSID)
  {
    Config.apid = portalSSID;
  }
  Config.psk="";

  Portal.config(Config);

  // Load aux. page
  Ip.load(AUX_IPCONFIG);
  Portal.join({ Ip });        // Register aux. page

  // Behavior a root path of ESP8266WebServer.
  acServer.on("/", rootPage);
  acServer.on("/saveip", saveIpPage); 
  if (onDetect) 
  {
    Portal.onDetect(onDetect);  // Register onDetect exit routine.
  }

  // Establish a connection with an autoReconnect option.
  if (Portal. begin(NULL,NULL,10000)) {
    success = true;
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }


  Portal.end();
  //acServer.close();
  acServer.stop();
  Serial.println("End of autoconnect");

  return  success;
}