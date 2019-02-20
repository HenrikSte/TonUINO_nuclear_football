#include "vs1053_ext.h"
#include "stringarray.h"

#ifdef ARDUINO_ARCH_ESP32
    #include "esp32-hal-log.h"
#else
    static const char *TAG = "VS1053";
#endif

VS1053::VS1053(uint8_t _cs_pin, uint8_t _dcs_pin, uint8_t _dreq_pin) :
        cs_pin(_cs_pin), dcs_pin(_dcs_pin), dreq_pin(_dreq_pin)
{
    m_endFillByte=0;
    curvol=50;
    m_t0=0;
    m_LFcount=0;
}
VS1053::~VS1053()
{
    // destructor
}
//---------------------------------------------------------------------------------------
void VS1053::control_mode_on()
{
    SPI.beginTransaction(VS1053_SPI);           // Prevent other SPI users
    DCS_HIGH();                                 // Bring slave in control mode
    CS_LOW();
}
void VS1053::control_mode_off()
{
    CS_HIGH();                                  // End control mode
    SPI.endTransaction();                       // Allow other SPI users
}
void VS1053::data_mode_on()
{
    SPI.beginTransaction(VS1053_SPI);           // Prevent other SPI users
    CS_HIGH();                                  // Bring slave in data mode
    DCS_LOW();
}
void VS1053::data_mode_off()
{
    //digitalWrite(dcs_pin, HIGH);              // End data mode
    DCS_HIGH();
    SPI.endTransaction();                       // Allow other SPI users
}
//---------------------------------------------------------------------------------------
uint16_t VS1053::read_register(uint8_t _reg)
{
    uint16_t result=0;
    control_mode_on();
    SPI.write(3);                                // Read operation
    SPI.write(_reg);                             // Register to write (0..0xF)
    // Note: transfer16 does not seem to work
    result=(SPI.transfer(0xFF) << 8) | (SPI.transfer(0xFF));  // Read 16 bits data
    await_data_request();                        // Wait for DREQ to be HIGH again
    control_mode_off();
    return result;
}
//---------------------------------------------------------------------------------------
void VS1053::write_register(uint8_t _reg, uint16_t _value)
{
    control_mode_on();
    SPI.write(2);                                // Write operation
    SPI.write(_reg);                             // Register to write (0..0xF)
    SPI.write16(_value);                         // Send 16 bits data
    await_data_request();
    control_mode_off();
}
//---------------------------------------------------------------------------------------
void VS1053::sdi_send_buffer(uint8_t* data, size_t len)
{
    size_t chunk_length;                         // Length of chunk 32 byte or shorter

    data_mode_on();
    while(len){                                  // More to do?

        await_data_request();                    // Wait for space available
        chunk_length=len;
        if(len > vs1053_chunk_size){
            chunk_length=vs1053_chunk_size;
        }
        len-=chunk_length;
        SPI.writeBytes(data, chunk_length);
        data+=chunk_length;
    }
    data_mode_off();
}
//---------------------------------------------------------------------------------------
void VS1053::sdi_send_fillers(size_t len)
{
    size_t chunk_length;                         // Length of chunk 32 byte or shorter

    data_mode_on();
    while(len)                                   // More to do?
    {
        await_data_request();                    // Wait for space available
        chunk_length=len;
        if(len > vs1053_chunk_size){
            chunk_length=vs1053_chunk_size;
        }
        len-=chunk_length;
        while(chunk_length--){
            SPI.write(m_endFillByte);
        }
    }
    data_mode_off();
}
//---------------------------------------------------------------------------------------
void VS1053::wram_write(uint16_t address, uint16_t data){

    write_register(SCI_WRAMADDR, address);
    write_register(SCI_WRAM, data);
}
//---------------------------------------------------------------------------------------
uint16_t VS1053::wram_read(uint16_t address){

    write_register(SCI_WRAMADDR, address);       // Start reading from WRAM
    return read_register(SCI_WRAM);              // Read back result
}
//---------------------------------------------------------------------------------------
void VS1053::begin()
{
    pinMode(dreq_pin, INPUT);                          // DREQ is an input
    pinMode(cs_pin, OUTPUT);                           // The SCI and SDI signals
    pinMode(dcs_pin, OUTPUT);
    digitalWrite(dcs_pin, HIGH);                       // Start HIGH for SCI en SDI
    digitalWrite(cs_pin, HIGH);
    delay(100);

    // Init SPI in slow mode (0.2 MHz)
    VS1053_SPI=SPISettings(200000, MSBFIRST, SPI_MODE0);
    ESP_LOGV(TAG, "Right after reset/startup");

    delay(20);

    // Most VS1053 modules will start up in midi mode.  The result is that there is no audio
    // when playing MP3.  You can modify the board, but there is a more elegant way:
    wram_write(0xC017, 3);                             // GPIO DDR=3
    wram_write(0xC019, 0);                             // GPIO ODATA=0
    delay(100);
    //printDetails ("After test loop");
    ESP_LOGV(TAG, "soft reset");
    softReset();                                       // Do a soft reset
    // Switch on the analog parts
    write_register(SCI_AUDATA, 44100 + 1);             // 44.1kHz + stereo
    // The next clocksetting allows SPI clocking at 5 MHz, 4 MHz is safe then.
    write_register(SCI_CLOCKF, 6 << 12);               // Normal clock settings multiplyer 3.0=12.2 MHz
    //SPI Clock to 4 MHz. Now you can set high speed SPI clock.
    ESP_LOGV(TAG, "setting SPI to 4mhz");
    VS1053_SPI=SPISettings(4000000, MSBFIRST, SPI_MODE0);
    write_register(SCI_MODE, _BV (SM_SDINEW) | _BV(SM_LINE1));
    //testComm("Fast SPI, Testing VS1053 read/write registers again... \n");
    delay(10);
    ESP_LOGV(TAG, "await data request");
    await_data_request();
    m_endFillByte=wram_read(0x1E06) & 0xFF;
    ESP_LOGD(TAG, "endFillByte is 0x%X", m_endFillByte);
    delay(100);
}
//---------------------------------------------------------------------------------------

void VS1053::setVolume(uint8_t vol)
{
  // Set volume.  Both left and right.
  // Input value is 0..21.  21 is the loudest.
  // Clicking reduced by using 0xf8 to 0x00 as limits.
  uint16_t value;                                      // Value to send to SCI_VOL
  
  ESP_LOGV(TAG, "Changing Volume from %d to %d", curvol , vol);

  if(vol > 21) vol=21;

  if(vol != curvol){
      curvol=vol;                                      // Save for later use
      vol=volumetable[vol];                           // Convert via table
      value=map(vol, 0, 100, 0xF8, 0x00);              // 0..100% to one channel
      ESP_LOGV(TAG, "Volume %d in table is %d and mapped to %d", curvol, vol, value);
      value=(value << 8) | value;
      write_register(SCI_VOL, value);                  // Volume left and right
  }
}
/*
void VS1053::setVolume(uint8_t vol)
{
    // Set volume.  Both left and right.
    // Input value is 0..21.  21 is the loudest.
    // Clicking reduced by using 0xf8 to 0x00 as limits.
    uint16_t value;                                      // Value to send to SCI_VOL

    if(vol > 21)
    {
        vol=21;
    }
    vol=volumetable[vol];
    if(vol != curvol)
    {
        curvol=vol;                                      // Save for later use
        value=map(vol, 0, 100, 0xF8, 0x00);              // 0..100% to one channel
        value=(value << 8) | value;
        write_register(SCI_VOL, value);                  // Volume left and right
    }
}
*/
//---------------------------------------------------------------------------------------
void VS1053::setTone(uint8_t *rtone)
{                    // Set bass/treble (4 nibbles)

    // Set tone characteristics.  See documentation for the 4 nibbles.
    uint16_t value=0;                                    // Value to send to SCI_BASS
    int i;                                               // Loop control

    for(i=0; i < 4; i++)
    {
        value=(value << 4) | rtone[i];                   // Shift next nibble in
    }
    write_register(SCI_BASS, value);                     // Volume left and right
}
//---------------------------------------------------------------------------------------
uint8_t VS1053::getVolume()                              // Get the currenet volume setting.
{
    return curvol;
}
//---------------------------------------------------------------------------------------
void VS1053::startSong()
{
    sdi_send_fillers(2052);
}
//---------------------------------------------------------------------------------------
void VS1053::stopSong()
{
    uint16_t modereg;                     // Read from mode register
    int i;                                // Loop control

    sdi_send_fillers(2052);
    delay(10);
    write_register(SCI_MODE, _BV (SM_SDINEW) | _BV(SM_CANCEL));
    for(i=0; i < 200; i++)
    {
        sdi_send_fillers(32);
        modereg=read_register(SCI_MODE);  // Read status
        if((modereg & _BV(SM_CANCEL)) == 0)
        {
            sdi_send_fillers(2052);
            ESP_LOGD(TAG, "Song stopped correctly after %d msec", i * 10);
            return;
        }
        delay(10);
    }
    ESP_LOGD(TAG, "Song stopped incorrectly!");
    printDetails();
}
//---------------------------------------------------------------------------------------
void VS1053::softReset()
{
    write_register(SCI_MODE, _BV (SM_SDINEW) | _BV(SM_RESET));
    delay(10);
    await_data_request();
}
//---------------------------------------------------------------------------------------
void VS1053::printDetails()
{
    uint16_t regbuf[16];
    uint8_t i;
    String reg, tmp;
    //    String bit_rep[16] = {
    //        [ 0] = "0000", [ 1] = "0001", [ 2] = "0010", [ 3] = "0011",
    //        [ 4] = "0100", [ 5] = "0101", [ 6] = "0110", [ 7] = "0111",
    //        [ 8] = "1000", [ 9] = "1001", [10] = "1010", [11] = "1011",
    //        [12] = "1100", [13] = "1101", [14] = "1110", [15] = "1111",
    //    };
    String regName[16] = {
        [ 0] = "MODE       ", [ 1] = "STATUS     ", [ 2] = "BASS       ", [ 3] = "CLOCKF     ",
        [ 4] = "DECODE_TIME", [ 5] = "AUDATA     ", [ 6] = "WRAM       ", [ 7] = "WRAMADDR   ",
        [ 8] = "HDAT0      ", [ 9] = "HDAT1      ", [10] = "AIADDR     ", [11] = "VOL        ",
        [12] = "AICTRL0    ", [13] = "AICTRL1    ", [14] = "AICTRL2    ", [15] = "AICTRL3    ",
    };

    ESP_LOGD(TAG, "REG         Contents   bin   hex \n");
    ESP_LOGD(TAG, "----------- ---------------- ----\n");
    for(i=0; i <= SCI_AICTRL3; i++){
        regbuf[i]=read_register(i);
    }
    for(i=0; i <= SCI_AICTRL3; i++){
        reg=regName[i]+ " ";
        tmp=String(regbuf[i],2); while(tmp.length()<16) tmp="0"+tmp; // convert regbuf to binary string
        reg=reg+tmp +" ";
        tmp=String(regbuf[i],16); tmp.toUpperCase(); while(tmp.length()<4) tmp="0"+tmp; // conv to hex
        reg=reg+tmp;
        ESP_LOGD(TAG, "%s",reg.c_str());
    }
}
//---------------------------------------------------------------------------------------
bool VS1053::printVersion()
{
    boolean flag=false;
    uint16_t reg1=0, reg2=0;
    reg1=wram_read(0x1E00);
    reg2=wram_read(0x1E01);

    if((reg1==0xFFFF)&&(reg2==0xFFFF))
    {
        reg1=0; 
        reg2=0;
    } // all high?, seems not connected
    else 
    {
        flag=true;
    }
    ESP_LOGD(TAG, "chipID = %d%d", reg1, reg2);

    reg1=wram_read(0x1E02) & 0xFF;
    if(reg1==0xFF) 
    {
        reg1=0; 
        flag=false;
    } // version too high

    ESP_LOGD(TAG, "version = %d", reg1);

    return flag;
}
//---------------------------------------------------------------------------------------
bool VS1053::chkhdrline(const char* str){
    char b;                                            // Byte examined
    int len=0;                                         // Lengte van de string

    while((b= *str++)){                                // Search to end of string
        len++;                                         // Update string length
        if( !isalpha(b)){                              // Alpha (a-z, A-Z)
            if(b != '-'){                              // Minus sign is allowed
                if((b == ':') || (b == ';')){          // Found a colon or semicolon?
                    return ((len > 5) && (len < 200)); // Yes, okay if length is okay
                }
                else{
                    return false;                      // Not a legal character
                }
            }
        }
    }
    return false;                                      // End of string without colon
}
//---------------------------------------------------------------------------------------
void VS1053::showstreamtitle(const char *ml, bool full)
{
    // example for ml:
    // StreamTitle='Oliver Frank - Mega Hitmix';StreamUrl='www.radio-welle-woerthersee.at';
    // or adw_ad='true';durationMilliseconds='10135';adId='34254';insertionType='preroll';

    int16_t pos1=0, pos2=0, pos3=0, pos4=0;
    String mline=ml, st="", su="", ad="", artist="", title="", icyurl="";
    //log_i("%s",mline.c_str());
    pos1=mline.indexOf("StreamTitle=");
    if(pos1!=-1){                                       // StreamTitle found
        pos1=pos1+12;
        st=mline.substring(pos1);                       // remove "StreamTitle="
    //  log_i("st_orig %s", st.c_str());
        if(st.startsWith("'{")){
            // special codig like '{"t":"\u041f\u0438\u043a\u043d\u0438\u043a - \u0418...."m":"mdb","lAU":0,"lAuU":18}
            pos2= st.indexOf('"', 8);                   // end of '{"t":".......", seek for double quote at pos 8
            st=st.substring(0, pos2);
            pos2= st.lastIndexOf('"');
            st=st.substring(pos2+1);                    // remove '{"t":"
            pos2=0;
            String uni="";
            String st1="";
            uint16_t u=0;
            uint8_t v=0, w=0;
            for(int i=0; i<st.length(); i++){
                if(pos2>1) pos2++;
                if((st[i]=='\\')&&(pos2==0)) pos2=1;    // backslash found
                if((st[i]=='u' )&&(pos2==1)) pos2=2;    // "\u" found
                if(pos2>2) uni=uni+st[i];               // next 4 values are unicode
                if(pos2==0) st1+=st[i];                 // normal character
                if(pos2>5){
                    pos2=0;
                    u=strtol(uni.c_str(), 0, 16);       // convert hex to int
                    v=u/64 + 0xC0; st1+=char(v);        // compute UTF-8
                    w=u%64 + 0x80; st1+=char(w);
                     //log_i("uni %i  %i", v, w );
                    uni="";
                }
            }
            log_i("st1 %s", st1.c_str());
            st=st1;
        }
        else{
            // normal coding
            if(st.indexOf('&')!=-1){                // maybe html coded
                st.replace("&Auml;", "Ä" ); st.replace("&auml;", "ä"); //HTML -> ASCII
                st.replace("&Ouml;", "Ö" ); st.replace("&ouml;", "o");
                st.replace("&Uuml;", "Ü" ); st.replace("&uuml;", "ü");
                st.replace("&szlig;","ß" ); st.replace("&amp;",  "&");
                st.replace("&quot;", "\""); st.replace("&lt;",   "<");
                st.replace("&gt;",   ">" ); st.replace("&apos;", "'");
            }
            pos2= st.indexOf(';',1);                // end of StreamTitle, first occurence of ';'
            if(pos2!=-1) st=st.substring(0,pos2);   // extract StreamTitle
            if(st.startsWith("'")) st=st.substring(1,st.length()-1); // if exists remove ' at the begin and end
            pos3=st.lastIndexOf(" - ");             // separator artist - title
            if(pos3!=-1){                           // found separator? yes
                artist=st.substring(0,pos3);        // artist not used yet
                title=st.substring(pos3+3);         // title not used yet
            }
        }

        if(m_st_remember!=st){ // show only changes
            if(vs1053_showstreamtitle) vs1053_showstreamtitle(st.c_str());
        }

        m_st_remember=st;
        st="StreamTitle=" + st + '\n';
        if(vs1053_info) vs1053_info(st.c_str());
    }
    pos4=mline.indexOf("StreamUrl=");
    if(pos4!=-1){                               // StreamUrl found
        pos4=pos4+10;
        su=mline.substring(pos4);               // remove "StreamUrl="
        pos2= su.indexOf(';',1);                // end of StreamUrl, first occurence of ';'
        if(pos2!=-1) su=su.substring(0,pos2);   // extract StreamUrl
        if(su.startsWith("'")) su=su.substring(1,su.length()-1); // if exists remove ' at the begin and end
        su="StreamUrl=" + su + '\n';
        if(vs1053_info) vs1053_info(su.c_str());
    }
    pos2=mline.indexOf("adw_ad=");              // advertising,
    if(pos2!=-1){
       ad=mline.substring(pos2);
       ad=ad + '\n';
       if(vs1053_info) vs1053_info(ad.c_str());
       pos2=mline.indexOf("durationMilliseconds=");
       if(pos2!=-1){
    	  pos2+=22;
    	  mline=mline.substring(pos2);
    	  mline=mline.substring(0, mline.indexOf("'")-3); // extract duration in sec
    	  if(vs1053_commercial) vs1053_commercial(mline.c_str());
       }
    }
    if(!full){
        m_icystreamtitle="";                    // Unknown type
        return;                                 // Do not show
    }
    if(pos1==-1 && pos4==-1)
    {
        // Info probably from playlist
        st=mline;
        ESP_LOGD(TAG, "Streamtitle: %s", st.c_str());
    }
}
//---------------------------------------------------------------------------------------
void VS1053::handlebyte(uint8_t b)
{
    static uint16_t playlistcnt;                                // Counter to find right entry in playlist
    String lcml;                                                // Lower case metaline
    static String ct;                                           // Contents type
    static String host;
    int inx;                                                    // Pointer in metaline
    static boolean f_entry=false;                               // entryflag for asx playlist

    if(m_datamode == VS1053_HEADER)                             // Handle next byte of MP3 header
    {
        if((b > 0x7F) ||                                        // Ignore unprintable characters
                (b == '\r') ||                                  // Ignore CR
                (b == '\0'))                                    // Ignore NULL
                {
            // Yes, ignore
        }
        else if(b == '\n'){                                     // Linefeed ?
            m_LFcount++;                                        // Count linefeeds
            if(chkhdrline(m_metaline.c_str())){                 // Reasonable input?
                lcml=m_metaline;                                // Use lower case for compare
                lcml.toLowerCase();
                lcml.trim();
                ESP_LOGD(TAG, "%s", m_metaline.c_str());      // Yes, Show it       
                if(lcml.indexOf("content-type:") >= 0){         // Line with "Content-Type: xxxx/yyy"
                    if(lcml.indexOf("audio") >= 0){             // Is ct audio?
                        m_ctseen=true;                          // Yes, remember seeing this
                        ct=m_metaline.substring(13);            // Set contentstype. Not used yet
                        ct.trim();
                        ESP_LOGD(TAG, "%s seen.", ct.c_str());
                    }
                    if(lcml.indexOf("ogg") >= 0){               // Is ct ogg?
                        m_ctseen=true;                          // Yes, remember seeing this
                        ct=m_metaline.substring(13);
                        ct.trim();
                        ESP_LOGD(TAG, "%s seen.", ct.c_str());
                        m_metaint=0;                            // ogg has no metadata
                        m_bitrate=0;
                        m_icyname=="";
                        m_f_ogg=true;
                    }
                }
                else if(lcml.startsWith("location:")){
                    host=m_metaline.substring(lcml.indexOf("http"),lcml.length());// use metaline instead lcml
                    if(host.indexOf("&")>0)host=host.substring(0,host.indexOf("&")); // remove parameter
                    ESP_LOGD(TAG, "redirect to new host %s", host.c_str());
                    connecttohost(host);
                }
                else if(lcml.startsWith("icy-br:")){
                    m_bitrate=m_metaline.substring(7).toInt();  // Found bitrate tag, read the bitrate
                    ESP_LOGD(TAG, "%d", m_bitrate);
                }
                else if(lcml.startsWith("icy-metaint:")){
                    m_metaint=m_metaline.substring(12).toInt(); // Found metaint tag, read the value
                    //if(m_metaint==0) m_metaint=16000;           // if no set to default
                }
                else if(lcml.startsWith("icy-name:")){
                    m_icyname=m_metaline.substring(9);          // Get station name
                    m_icyname.trim();                           // Remove leading and trailing spaces
                    if(m_icyname!=""){
                        if(vs1053_showstation) vs1053_showstation(m_icyname.c_str());
                    }
    //   for(int z=0; z<m_icyname.length();z++) log_e("%i",m_icyname[z]);
                }
                else if(lcml.startsWith("transfer-encoding:")){
                    // Station provides chunked transfer
                    if(m_metaline.endsWith("chunked")){
                        m_chunked=true;
                        ESP_LOGD(TAG, "chunked data transfer");
                        m_chunkcount=0;                         // Expect chunkcount in DATA
                    }
                }
                else if(lcml.startsWith("icy-url:")){
                    m_icyurl=m_metaline.substring(8);             // Get the URL
                    m_icyurl.trim();
                    if(vs1053_icyurl) vs1053_icyurl(m_icyurl.c_str());
                }
                else{
                    // all other
                }
            }
            m_metaline="";                                      // Reset this line
            if((m_LFcount == 2) && m_ctseen){                   // Some data seen and a double LF?
                if(m_icyname==""){if(vs1053_showstation) vs1053_showstation("");} // no icyname available
                if(m_bitrate==0){if(vs1053_bitrate) vs1053_bitrate("");} // no bitrate received
                if(m_f_ogg==true){
                    m_datamode=VS1053_OGG;                      // Overwrite m_datamode
                    ESP_LOGD(TAG, "Switch to OGG, bitrate is %d, metaint is %d", m_bitrate, m_metaint); // Show bitrate and metaint
                    String lasthost=m_lastHost;
                    uint idx=lasthost.indexOf('?');
                    if(idx>0) lasthost=lasthost.substring(0, idx);
                    if(vs1053_lasthost) vs1053_lasthost(lasthost.c_str());
                    m_f_ogg=false;
                }
                else{
                    m_datamode=VS1053_DATA;                         // Expecting data now
                    ESP_LOGD(TAG, "Switch to DATA, bitrate is %d, metaint is %d", m_bitrate, m_metaint); // Show bitrate and metaint
                    String lasthost=m_lastHost;
                    uint idx=lasthost.indexOf('?');
                    if(idx>0) lasthost=lasthost.substring(0, idx);
                    if(vs1053_lasthost) vs1053_lasthost(lasthost.c_str());
                }
                startSong();                                    // Start a new song
                delay(1000);
            }
        }
        else
        {
            m_metaline+=(char)b;                                // Normal character, put new char in metaline
            m_LFcount=0;                                        // Reset double CRLF detection
        }
        return;
    }
    if(m_datamode == VS1053_METADATA)                           // Handle next byte of metadata
    {
        if(m_firstmetabyte)                                     // First byte of metadata?
        {
            m_firstmetabyte=false;                              // Not the first anymore
            m_metacount=b * 16 + 1;                             // New count for metadata including length byte
            if(m_metacount > 1)
            {
                ESP_LOGD(TAG, "Metadata block %d bytes",      // Most of the time there are zero bytes of metadata
                        m_metacount-1);
            }
            m_metaline="";                                      // Set to empty
        }
        else
        {
            m_metaline+=(char)b;                                // Normal character, put new char in metaline
        }
        if(--m_metacount == 0){
            if(m_metaline.length()){                            // Any info present?
                // metaline contains artist and song name.  For example:
                // "StreamTitle='Don McLean - American Pie';StreamUrl='';"
                // Sometimes it is just other info like:
                // "StreamTitle='60s 03 05 Magic60s';StreamUrl='';"
                // Isolate the StreamTitle, remove leading and trailing quotes if present.
                //log_i("ST %s", m_metaline.c_str());
            	if( !m_f_localfile) showstreamtitle(m_metaline.c_str(), true);         // Show artist and title if present in metadata
            }
            if(m_metaline.length() > 1500){                     // Unlikely metaline length?
                ESP_LOGD(TAG, "Metadata block to long! Skipping all Metadata from now on.");
                m_metaint=16000;                                // Probably no metadata
                m_metaline="";                                  // Do not waste memory on this
            }
            m_datamode=VS1053_DATA;                             // Expecting data
        }
    }
    if(m_datamode == VS1053_PLAYLISTINIT)                       // Initialize for receive .m3u file
    {
        // We are going to use metadata to read the lines from the .m3u file
        // Sometimes this will only contain a single line
        f_entry=false;                                          // no entry found yet (asx playlist)
        m_metaline="";                                          // Prepare for new line
        m_LFcount=0;                                            // For detection end of header
        m_datamode=VS1053_PLAYLISTHEADER;                       // Handle playlist data
        playlistcnt=1;                                          // Reset for compare
        m_totalcount=0;                                         // Reset totalcount
        ESP_LOGD(TAG, "Read from playlist");
    }
    if(m_datamode == VS1053_PLAYLISTHEADER){                    // Read header
        if((b > 0x7F) ||                                        // Ignore unprintable characters
                (b == '\r') ||                                  // Ignore CR
                (b == '\0'))                                    // Ignore NULL
        {
            // Yes, ignore
        }
        else if(b == '\n')                                      // Linefeed ?
        {
            m_LFcount++;                                        // Count linefeeds
            ESP_LOGD(TAG, "Playlistheader: %s", m_metaline.c_str());  // Show playlistheader
            lcml=m_metaline;                                // Use lower case for compare
            lcml.toLowerCase();
            lcml.trim();
            if(lcml.startsWith("location:")){
                 host=m_metaline.substring(lcml.indexOf("http"),lcml.length());// use metaline instead lcml
                if(host.indexOf("&")>0)host=host.substring(0,host.indexOf("&")); // remove parameter
                ESP_LOGD(TAG, "redirect to new host %s", host.c_str());
                connecttohost(host);
            }
            m_metaline="";                                      // Ready for next line
            if(m_LFcount == 2)
                    {
                ESP_LOGD(TAG, "Switch to PLAYLISTDATA");
                m_datamode=VS1053_PLAYLISTDATA;                 // Expecting data now
                return;
            }
        }
        else
        {
            m_metaline+=(char)b;                                // Normal character, put new char in metaline
            m_LFcount=0;                                        // Reset double CRLF detection
        }
    }
    if(m_datamode == VS1053_PLAYLISTDATA)                       // Read next byte of .m3u file data
    {
        m_t0=millis();
        if((b > 0x7F) ||                                        // Ignore unprintable characters
                (b == '\r') ||                                  // Ignore CR
                (b == '\0'))                                    // Ignore NULL
                { /* Yes, ignore */ }

        else if(b == '\n'){                                     // Linefeed or end of string?
            ESP_LOGD(TAG, "Playlistdata: %s", m_metaline.c_str());  // Show playlistdata
            if(m_playlist.endsWith("m3u")){
                if(m_metaline.length() < 5) {                   // Skip short lines
                    m_metaline="";                              // Flush line
                    return;}
                if(m_metaline.indexOf("#EXTINF:") >= 0){        // Info?
                    if(m_playlist_num == playlistcnt){          // Info for this entry?
                        inx=m_metaline.indexOf(",");            // Comma in this line?
                        if(inx > 0){
                            // Show artist and title if present in metadata
                            //if(vs1053_showstation) vs1053_showstation(m_metaline.substring(inx + 1).c_str());
                            if(vs1053_info) vs1053_info(m_metaline.substring(inx + 1).c_str());
                        }
                    }
                }
                if(m_metaline.startsWith("#")){                 // Commentline?
                    m_metaline="";
                    return;}                                    // Ignore commentlines
                // Now we have an URL for a .mp3 file or stream.  Is it the rigth one?
                //if(metaline.indexOf("&")>0)metaline=host.substring(0,metaline.indexOf("&"));
                ESP_LOGD(TAG, "Entry %d in playlist found: %s", playlistcnt, m_metaline.c_str());
                if(m_metaline.indexOf("&")){
                    m_metaline=m_metaline.substring(0, m_metaline.indexOf("&"));}
                if(m_playlist_num == playlistcnt){
                    inx=m_metaline.indexOf("http://");          // Search for "http://"
                    if(inx >= 0){                               // Does URL contain "http://"?
                        host=m_metaline.substring(inx + 7);}    // Yes, remove it and set host
                    else{
                        host=m_metaline;}                       // Yes, set new host
                    //log_i("connecttohost %s", host.c_str());
                    connecttohost(host);                        // Connect to it
                }
                m_metaline="";
                host=m_playlist;                                // Back to the .m3u host
                playlistcnt++;                                  // Next entry in playlist
            } //m3u
            if(m_playlist.endsWith("pls")){
                if(m_metaline.startsWith("File1")){
                    inx=m_metaline.indexOf("http://");          // Search for "http://"
                    if(inx >= 0){                               // Does URL contain "http://"?
                        m_plsURL=m_metaline.substring(inx + 7); // Yes, remove it
                        if(m_plsURL.indexOf("&")>0)m_plsURL=m_plsURL.substring(0,m_plsURL.indexOf("&")); // remove parameter
                        // Now we have an URL for a .mp3 file or stream in host.

                        m_f_plsFile=true;
                    }
                }
                if(m_metaline.startsWith("Title1")){
                    m_plsStationName=m_metaline.substring(7);
                    if(vs1053_showstation) vs1053_showstation(m_plsStationName.c_str());
                    ESP_LOGD(TAG, "StationName: %s", m_plsStationName.c_str());
                    m_f_plsTitle=true;
                }
                if(m_metaline.startsWith("Length1")) m_f_plsTitle=true; // if no Title is available
                if((m_f_plsFile==true)&&(m_metaline.length()==0)) m_f_plsTitle=true;
                m_metaline="";
                if(m_f_plsFile && m_f_plsTitle){    //we have both StationName and StationURL
                    m_f_plsFile=false; m_f_plsTitle=false;
                    //log_i("connecttohost %s", m_plsURL.c_str());
                    connecttohost(m_plsURL);        // Connect to it
                }
            }//pls
            if(m_playlist.endsWith("asx")){
                String ml=m_metaline;
                ml.toLowerCase();                               // use lowercases
                if(ml.indexOf("<entry>")>=0) f_entry=true;      // found entry tag (returns -1 if not found)
                if(f_entry){
                    if(ml.indexOf("ref href")>0){
                        inx=ml.indexOf("http://");
                        if(inx>0){
                            m_plsURL=m_metaline.substring(inx + 7); // Yes, remove it
                            if(m_plsURL.indexOf('"')>0)m_plsURL=m_plsURL.substring(0,m_plsURL.indexOf('"')); // remove rest
                            // Now we have an URL for a stream in host.
                            m_f_plsFile=true;
                        }
                    }
                    if(ml.indexOf("<title>")>=0){
                        m_plsStationName=m_metaline.substring(7);
                        if(m_plsURL.indexOf('<')>0)m_plsURL=m_plsURL.substring(0,m_plsURL.indexOf('<')); // remove rest
                        if(vs1053_showstation) vs1053_showstation(m_plsStationName.c_str());
                        ESP_LOGD(TAG, "StationName: %s", m_plsStationName.c_str());
                        m_f_plsTitle=true;
                    }
                }//entry
                m_metaline="";
                if(m_f_plsFile && m_f_plsTitle){   //we have both StationName and StationURL
                    m_f_plsFile=false; m_f_plsTitle=false;
                    //log_i("connecttohost %s", host.c_str());
                    connecttohost(m_plsURL);        // Connect to it
                }
            }//asx
        }
        else
        {
            m_metaline+=(char)b;                            // Normal character, add it to metaline
        }
        return;
    }
}
//---------------------------------------------------------------------------------------
uint16_t VS1053::ringused()
{
    return (m_rcount);                                      // Free space available
}
//---------------------------------------------------------------------------------------

bool VS1053::isPlaying()
{
  return bool(mp3file);
}

void VS1053::nextTrack()
{
  if (m_f_localfile && mp3file)
  {
    mp3file.seek(mp3file.size());
  }
}

void VS1053::loop()
{

    uint16_t part=0;                                        // part at the end of the ringbuffer
    uint16_t bcs=0;                                         // bytes can current send
    uint16_t maxchunk=0x1000;                               // max number of bytes to read, 4096d is enough
    uint16_t btp=0;                                         // bytes to play
    int16_t  res=0;                                         // number of bytes getting from client
    uint32_t av=0;                                          // available in stream (uin16_t is to small by playing from SD)
    static uint16_t rcount=0;                               // max bytes handover to the player
    static uint16_t chunksize=0;                            // Chunkcount read from stream
    static uint16_t count=0;                                // Bytecounter between metadata
    static uint32_t i=0;                                    // Count loops if ringbuffer is empty

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_f_localfile)                                       // Playing file from SD card?
    {
        av=mp3file.available();                             // Bytes left in file
        if(av < maxchunk) maxchunk=av;                      // Reduce byte count for this mp3loop()
        if(maxchunk)                                        // Anything to read?
        {
            m_btp=mp3file.read(m_ringbuf, maxchunk);        // Read a block of data
            sdi_send_buffer(m_ringbuf,m_btp);
        }
        if(av == 0)
        {                                                   // No more data from SD Card
            ESP_LOGD(TAG, "End of mp3file %s",m_mp3title.c_str());
            if(m_playlist.length()) 
            {
                bool result = true;

                //close the actual file
                mp3file.close();

                //increment the entry and 
                m_playlist_num++;

                //get next file from playlist (if it exists)
                String nextTitle = findNextPlaylistEntry();
                if (nextTitle.length())
                {
                  if (nextTitle.substring(nextTitle.lastIndexOf('.') + 1, nextTitle.length()).equalsIgnoreCase("mp3"))
                  {
                      ESP_LOGI(TAG, "Playing next Entry from playlist \"%s\"", nextTitle.c_str());

                      m_mp3title=nextTitle.substring(nextTitle.lastIndexOf('/') + 1, nextTitle.length());

                      result = openMp3File(nextTitle, 0);
                  } 
                  else 
                  {
                      ESP_LOGW(TAG, "Invalid Entry from playlist \"%s\"", nextTitle.c_str());
                  }
                }
                else
                {
                  ESP_LOGV(TAG, "End of playlist");
                  result = false;
                }
                
                if ( result == false)
                {
                    stop_mp3client(true);
                }
            }
            else
            {
                stop_mp3client(true);
            }
        }
    }
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    if(m_f_webstream){                                      // Playing file from URL?
        if(m_ssl==false) av=client.available();// Available from stream
        if(m_ssl==true)  av=clientsecure.available();// Available from stream
        if(av)
        {
            m_ringspace=m_ringbfsiz - m_rcount;
            part=m_ringbfsiz - m_rbwindex;                  // Part of length to xfer
            if(part>m_ringspace)part=m_ringspace;
            if(m_ssl==false) res=client.read(m_ringbuf+ m_rbwindex, part);   // Copy first part
            if(m_ssl==true)  res=clientsecure.read(m_ringbuf+ m_rbwindex, part);   // Copy first part
            if(res>0)
            {
                m_rcount+=res;
                m_rbwindex+=res;
            }
            if(m_rbwindex==m_ringbfsiz) m_rbwindex=0;
        }
        if(m_datamode == VS1053_PLAYLISTDATA){
            if(m_t0+49<millis()) {
                //log_i("terminate metaline after 50ms");     // if mo data comes from host
                handlebyte('\n');                           // send LF
            }
        }
        if(m_chunked==false){rcount=m_rcount;}
        else{
            while(m_rcount){
                if((m_chunkcount+rcount) == 0|| m_firstchunk){             // Expecting a new chunkcount?
                    uint8_t b =m_ringbuf[m_rbrindex];
                    if(b=='\r'){}
                    else if(b=='\n'){
                        m_chunkcount=chunksize;
                        m_firstchunk=false;
                        rcount=0;
                        chunksize=0;
                    }
                    else{
                        // We have received a hexadecimal character.  Decode it and add to the result.
                        b=toupper(b) - '0';                         // Be sure we have uppercase
                        if(b > 9) b = b - 7;                        // Translate A..F to 10..15
                        chunksize=(chunksize << 4) + b;
                    }
                    if(++m_rbrindex == m_ringbfsiz){        // Increment pointer and
                        m_rbrindex=0;                       // wrap at end
                    }
                    m_rcount--;

                }
                else break;
            }
            if(rcount==0){ //all bytes consumed?
                if(m_chunkcount>m_rcount){
                    m_chunkcount-=m_rcount;
                    rcount=m_rcount;
                }
                else{
                    rcount=m_chunkcount;
                    m_chunkcount-=rcount;
                }
            }
        }

        //*******************************************************************************

        if(m_datamode==VS1053_OGG){
            if(rcount>1024) btp=1024; else btp=rcount;  // reduce chunk thereby the ringbuffer can be proper fillied
            if(btp){  //bytes to play
                rcount-=btp;
                if((m_rbrindex + btp) >= m_ringbfsiz){
                    part=m_ringbfsiz - m_rbrindex;
                    sdi_send_buffer(m_ringbuf+ m_rbrindex, part);
                    m_rbrindex=0;
                    m_rcount-=part;
                    btp-=part;
                }
                if(btp){                                         // Rest to do?
                    sdi_send_buffer(m_ringbuf+ m_rbrindex, btp); // Copy full or last part
                    m_rbrindex+=btp;                             // Point to next free byte
                    m_rcount-=btp;                               // Adjust number of bytes
                }
            } return;
        }
        if(m_datamode==VS1053_DATA){
            if(rcount>1024)btp=1024;  else btp=rcount;  // reduce chunk thereby the ringbuffer can be proper fillied
            if(count>btp){bcs=btp; count-=bcs;} else{bcs=count; count=0;}
            if(bcs){ // bytes can send
              rcount-=bcs;
                // First see if we must split the transfer.  We cannot write past the ringbuffer end.
                if((m_rbrindex + bcs) >= m_ringbfsiz){
                    part=m_ringbfsiz - m_rbrindex;              // Part of length to xfer
                    sdi_send_buffer(m_ringbuf+ m_rbrindex, part);  // Copy first part
                    m_rbrindex=0;
                    m_rcount-=part;                             // Adjust number of bytes
                    bcs-=part;                                  // Adjust rest length
                }
                if(bcs){                                        // Rest to do?
                    sdi_send_buffer(m_ringbuf+ m_rbrindex, bcs); // Copy full or last part
                    m_rbrindex+=bcs;                            // Point to next free byte
                    m_rcount-=bcs;                              // Adjust number of bytes
                }
                if(count==0){
                    m_datamode=VS1053_METADATA;
                    m_firstmetabyte=true;
                }
            }
        }
        else{ //!=DATA
            while(rcount){
                handlebyte(m_ringbuf[m_rbrindex]);
                if(++m_rbrindex == m_ringbfsiz){                // Increment pointer and
                    m_rbrindex=0;                               // wrap at end
                }
                rcount--;
                // call handlebyte>connecttohost can set m_rcount to zero (empty ringbuff)
                if(m_rcount>0) m_rcount--;                   // no underrun
                if(m_rcount==0)rcount=0; // exit this while()
                if(m_datamode==VS1053_DATA){
                    count=m_metaint;
                    if(m_metaint==0) m_datamode=VS1053_OGG; // is likely no ogg but a stream without metadata, can be mms
                    break;
                }
            }
        }
        if((m_f_stream_ready==false)&&(ringused()!=0)){ // first streamdata recognised
            m_f_stream_ready=true;
        }
        if(m_f_stream_ready==true){
            if(ringused()==0){  // empty buffer, broken stream or bad bitrate?
                i++;
                if(i>150000){    // wait several seconds
                    i=0;
                    ESP_LOGD(TAG, "Stream lost -> try new connection");
                    connecttohost(m_lastHost);} // try a new connection
            }
            else i=0;
        }
    } // end if(webstream)
}
//---------------------------------------------------------------------------------------
void VS1053::stop_mp3client(bool resetPosition)
{
    /*
    uint16_t actualVolume = read_register(SCI_VOL);
    write_register(SCI_VOL, 0);                             // Mute while stopping
    */

    uint16_t actualVolume = getVolume();
    setVolume(0);

    ESP_LOGV(TAG, "stopping song");
    stopSong();

    fs::FS &fs=SD;
    File myTempFile;
    String positionFileName;
    size_t mp3Position=0;

    if (mp3file)
    {
        ESP_LOGV(TAG, "Stopping song, Current File: %s", mp3file.name());
        ESP_LOGV(TAG, "Current File: %s", mp3file.name());
        ESP_LOGD(TAG, "Current File position: %d", mp3file.position());
        positionFileName = mp3file.name();
        mp3Position      = mp3file.position();
        mp3file.close();
    }

    if (m_playlist.length()) // we are in playlist mode
    {
        ESP_LOGV(TAG, "Current Playlist: %s", m_playlist.c_str());
        ESP_LOGD(TAG, "Current Playlist position: %d", m_playlist_num);
        positionFileName = m_playlist.substring(0,m_playlist.length() - 4) + ".pos";
    }
    else if (positionFileName.length()) // we are in mp3 mode
    {
        positionFileName = positionFileName.substring(0,positionFileName.length() - 4) + ".pos";
    }

    if (positionFileName.length())
    {
      ESP_LOGV(TAG, "Position File Name: %s", positionFileName.c_str());
      myTempFile = fs.open(positionFileName, FILE_WRITE);
      if(myTempFile)
      {
          myTempFile.print("File Position:");

          if(resetPosition)
          {
              ESP_LOGV(TAG, "resetting file postion");
              myTempFile.println(0);
          } 
          else
          {
              myTempFile.println(mp3Position);
          }

          ESP_LOGV(TAG, "Successfully saved file position");

          myTempFile.print("Playlist:");

          if(resetPosition)
          {
              ESP_LOGV(TAG, "resetting playlist position");
              myTempFile.println(0);
          } 
          else
          {
              myTempFile.println(m_playlist_num);
          }

          ESP_LOGV(TAG, "Successfully saved playlist position");

          myTempFile.close();
      } 
      else 
      {
          ESP_LOGE(TAG, "Writing file position failed %s", positionFileName.c_str());
      }
    }
    

    m_f_localfile=false;
    m_f_webstream=false;
    
    m_playlist_num = 0;
    m_playlist     = "";

    client.flush();                                         // Flush stream client
    client.stop();                                          // Stop stream client

    setVolume(actualVolume);                  // restore the volume
}
//---------------------------------------------------------------------------------------
bool VS1053::connecttohost(String host)
{

    int inx;                                              // Position of ":" in hostname
    int port=80;                                          // Port number for host
    String extension="/";                                 // May be like "/mp3" in "skonto.ls.lv:8002/mp3"
    String hostwoext;                                     // Host without extension and portnumber
    String headerdata="";
    stopSong();
    stop_mp3client();                                     // Disconnect if still connected
    m_f_localfile=false;
    m_f_webstream=true;
    if(m_lastHost!=host){                                 // New host or reconnection?
        m_f_stream_ready=false;
        m_lastHost=host;                                  // Remember the current host
    }
    ESP_LOGD(TAG, "Connect to new host: %s", host.c_str());

    // initializationsequence
    m_rcount=0;                                             // Empty ringbuff
    m_rbrindex=0;
    m_rbwindex=0;
    m_ctseen=false;                                         // Contents type not seen yet
    m_metaint=0;                                            // No metaint yet
    m_LFcount=0;                                            // For detection end of header
    m_bitrate=0;                                            // Bitrate still unknown
    m_totalcount=0;                                         // Reset totalcount
    m_metaline="";                                          // No metadata yet
    m_icyname="";                                           // No StationName yet
    m_st_remember="";                                       // Delete the last streamtitle
    m_bitrate=0;                                            // No bitrate yet
    m_firstchunk=true;                                      // First chunk expected
    m_chunked=false;                                        // Assume not chunked
    m_ssl=false;
    setDatamode(VS1053_HEADER);                             // Handle header

    if(host.startsWith("http://")) {host=host.substring(7); m_ssl=false; ;}
    if(host.startsWith("https://")){host=host.substring(8); m_ssl=true;}
    clientsecure.stop(); clientsecure.flush(); // release memory

    if(host.endsWith(".m3u")||
        host.endsWith(".pls")||
        host.endsWith("asx"))                     // Is it an m3u or pls or asx playlist?
    {
        m_playlist=host;                                    // Save copy of playlist URL
        m_datamode=VS1053_PLAYLISTINIT;                     // Yes, start in PLAYLIST mode
        if(m_playlist_num == 0)                             // First entry to play?
        {
            m_playlist_num=1;                               // Yes, set index
        }
        ESP_LOGD(TAG, "Playlist request, entry %d", m_playlist_num); // Most of the time there are zero bytes of metadata
    }

    // In the URL there may be an extension, like noisefm.ru:8000/play.m3u&t=.m3u
    inx=host.indexOf("/");                                  // Search for begin of extension
    if(inx > 0){                                            // Is there an extension?
        extension=host.substring(inx);                      // Yes, change the default
        hostwoext=host.substring(0, inx);                   // Host without extension

    }
    // In the URL there may be a portnumber
    inx=host.indexOf(":");                                  // Search for separator
    if(inx >= 0){                                           // Portnumber available?
        port=host.substring(inx + 1).toInt();               // Get portnumber as integer
        hostwoext=host.substring(0, inx);                   // Host without portnumber
    }
    ESP_LOGD(TAG, "Connect to %s on port %d, extension %s",
            hostwoext.c_str(), port, extension.c_str());

    String resp=String("GET ") + extension +
                String(" HTTP/1.1\r\n") +
                String("Host: ") + hostwoext +
                String("\r\n") +
                String("Icy-MetaData:1\r\n") +
                String("Connection: close\r\n\r\n");

    if(m_ssl==false){
        if(client.connect(hostwoext.c_str(), port)){
            ESP_LOGD(TAG, "Connected to server");
            client.print(resp);
            return true;
        }
    }
    if(m_ssl==true){
        if(clientsecure.connect(hostwoext.c_str(), 443)){
            ESP_LOGD(TAG, "SSL/TLS Connected to server");
            clientsecure.print(resp);
            return true;
        }
    }

    ESP_LOGD(TAG, "Request %s failed!", host.c_str());
    if(vs1053_showstation) vs1053_showstation("");
    if(vs1053_showstreamtitle) vs1053_showstreamtitle("");
    if(vs1053_showstreaminfo) vs1053_showstreaminfo("");
    return false;
}

bool VS1053::makeM3uFile(const String& path, const String& m3uFileName )
{
    fs::FS &fs = SD;
    
    std::unique_ptr<StringArray> stringArray(new StringArray);

    File root = fs.open(path);
    if(!root){
        Serial.println("Failed to open directory");
        return false;
    }
    if(!root.isDirectory()){
        Serial.println("Not a directory");
        return false;
    }

    ESP_LOGD(TAG, "opening new m3u file: %s", m3uFileName.c_str());
    File m3u = fs.open(m3uFileName, "w");
    if (m3u)
    {
      File file = root.openNextFile();
      while(file){
          if(file.isDirectory()){
              Serial.print("  DIR : ");
              Serial.println(file.name());
              stringArray.get()->add(file.name());
              /*
              if(levels){
                  listDir(fs, file.name(), levels -1);
              }
              */
          } else {
              Serial.print("  FILE: ");
              Serial.print(file.name());
              Serial.print("  SIZE: ");
              Serial.println(file.size());
              String fileName = file.name();
              fileName.toUpperCase();
              if (fileName.endsWith(".MP3") 
              || fileName.endsWith(".M4A"))
              {
                stringArray.get()->add(file.name());
              }
          }
          file = root.openNextFile();
      }
      stringArray.get()->sort();
      return stringArray.get()->save(&m3u);
    }
    return false;
}


//---------------------------------------------------------------------------------------
bool VS1053::connecttoSD(String originalSdFile, bool resume)
{
    const uint8_t ascii[60]={
          //196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215,   ISO
            142, 143, 146, 128, 000, 144, 000, 000, 000, 000, 000, 000, 000, 165, 000, 000, 000, 000, 153, 000, //ASCII
          //216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235,   ISO
            000, 000, 000, 000, 154, 000, 000, 225, 133, 000, 000, 000, 132, 143, 145, 135, 138, 130, 136, 137, //ASCII
          //236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255    ISO
            000, 161, 140, 139, 000, 164, 000, 162, 147, 000, 148, 000, 000, 000, 163, 150, 129, 000, 000, 152};//ASCII

    String sdfile = originalSdFile;
    char path[256];
    uint16_t i=0, s=0;
    uint32_t position = 0;
    uint32_t playlist = 0;
    String fileExtension;
    bool result = false;
    //EventBits_t     bitFlags = 0;

    fs::FS &fs=SD;
    
    File file = fs.open(sdfile,"r");
    if (file.isDirectory())
    {
      file.close();
      String m3uFilename = sdfile+"/playlist.m3u";
      ESP_LOGD(TAG, "looking for: %s", m3uFilename.c_str());
      if (fs.exists(sdfile+"/playlist.m3u"))
      {
        ESP_LOGD(TAG, "found m3u file");
        sdfile= m3uFilename;
      }
      else
      {
        makeM3uFile(sdfile, m3uFilename);
        sdfile= m3uFilename;
      }
    }

    stop_mp3client();                           // Disconnect if still connected
    clientsecure.stop();                        // release memory if allocated
    clientsecure.flush(); 

    m_f_localfile=true;
    m_f_webstream=false;

    while(sdfile[i] != 0){                      //convert UTF8 to ASCII
        path[i]=sdfile[i];
        if(path[i] > 195){
            s=ascii[path[i]-196];
            if(s!=0) path[i]=s;                 // found a related ASCII sign
        } i++;
    }
    path[i]=0;
    ESP_LOGD(TAG, "Reading file: %s", path);
    //

    // get the file extension
    fileExtension = sdfile.substring(sdfile.lastIndexOf('.') + 1, sdfile.length());

    ESP_LOGV(TAG, "Play File with Extension \"%s\"", fileExtension.c_str());


    if ((resume) && (fs.exists(sdfile.substring(0,sdfile.length() - 4) + ".pos")) ) 
    {
        File myTempFile;

        ESP_LOGV(TAG, "Resuming track...");
        myTempFile = fs.open(sdfile.substring(0,sdfile.length() - 4) + ".pos");

        if (myTempFile) 
        {
            if (myTempFile.find("File Position:")) 
            {
                position = myTempFile.parseInt();

                ESP_LOGD(TAG, "Resume playing at position %u", position);
            } 
            else
            {
                ESP_LOGD(TAG, "Position Identification not found");
            }

            myTempFile.seek(0);
            
            if (myTempFile.find("Playlist:")) 
            {
                playlist = myTempFile.parseInt();

                ESP_LOGD(TAG, "Resume playlist at entry %u", playlist);
            } 
            else
            {
                ESP_LOGD(TAG, "Playlist Identification not found");
            }

            myTempFile.close();
        }
    }


    if (fileExtension.equalsIgnoreCase("MP3"))
    {
        m_mp3title=sdfile.substring(sdfile.lastIndexOf('/') + 1, sdfile.length());

        result = openMp3File(path, position);

        // bitFlags = SF_PLAYING_FILE;
    }
    else if (fileExtension.equalsIgnoreCase("M3U"))
    {
        // save the playlist path and start with the resumed entry
        m_playlist      = path;
        m_playlist_num  = playlist;

        String actualEntry = findNextPlaylistEntry(true);

        //send the file to the player
        if(actualEntry.substring(actualEntry.lastIndexOf('.') + 1, actualEntry.length()).equalsIgnoreCase("mp3"))
        {
            ESP_LOGI(TAG, "Playing Entry from playlist \"%s\"", actualEntry.c_str());

            m_mp3title=actualEntry.substring(actualEntry.lastIndexOf('/') + 1, actualEntry.length());

            result = openMp3File(actualEntry, position);
        } 
        else 
        {
            ESP_LOGW(TAG, "Invalid Entry from playlist \"%s\"", actualEntry.c_str());
        }

        // bitFlags = SF_PLAYING_FILE | SF_PLAYING_AUDIOBOOK;
    }

/*
    if ((result) && (m_SystemFlagGroup))
    {
        xEventGroupSetBits(m_SystemFlagGroup, bitFlags);

        showstreamtitle(m_mp3title.c_str(), true);
    }
*/
    return result;
}


String VS1053::findNextPlaylistEntry( bool restart )
{
    fs::FS  &fs=SD;
    File     myPlaylistFile;
    String   actualEntry = "";

    ESP_LOGD(TAG, "Analysing playlist %s", m_playlist.c_str());

    myPlaylistFile = fs.open(m_playlist);

    if (myPlaylistFile)
    {
        uint32_t actualEntryNumber = 0;

        ESP_LOGD(TAG, "looking for line %u", m_playlist_num+1);

        //read lines untill the "counter" matches the line
        while (actualEntryNumber <= m_playlist_num)
        {
            //read the next line
            actualEntry = myPlaylistFile.readStringUntil('\r');

            //remove whitespaces
            actualEntry.trim();

            //
            actualEntryNumber++;

            //check if we have reached the end of the list without finding our number
            if ((actualEntry.length() == 0) && (myPlaylistFile.available() == false))
            {
                /*
                ESP_LOGV(TAG, "Line %u does not exist, using line 1", m_playlist_num);

                //rewind the file
                myPlaylistFile.seek(0);
                actualEntryNumber   = 0;
                m_playlist_num      = 0;
                actualEntry         = "";
                */
                m_playlist_num      = 0;
                if (restart == false)
                {
                    break;
                }
                ESP_LOGV(TAG, "End of playlist");
            }
            else
            {
                ESP_LOGV(TAG, "Read playlist entry %u: \"%s\"", actualEntryNumber, actualEntry.c_str());
            }
        };

        myPlaylistFile.close();
    }

    return actualEntry;
}


//---------------------------------------------------------------------------------------
bool VS1053::openMp3File(String sdfile, uint32_t position) 
{
    bool result = true;

    fs::FS &fs=SD;
    mp3file=fs.open(sdfile);
    if(!mp3file)
    {
        ESP_LOGE(TAG, "Failed to open file %s for reading", sdfile);
        result = false;
    }
    else 
    {
        mp3file.seek(position);
    }
    return result;
}
//---------------------------------------------------------------------------------------
bool VS1053::connecttospeech(String speech, String lang)
{
    String host="translate.google.com";
    String path="/translate_tts";
    m_f_localfile=false;
    m_f_webstream=false;
    m_ssl=true;

    stopSong();
    stop_mp3client();                           // Disconnect if still connected
    clientsecure.stop(); clientsecure.flush();  // release memory if allocated

    String resp=   String("GET / HTTP/1.0\r\n") +
                   String("Host: ") + host + String("\r\n") +
                   String("User-Agent: GoogleTTS for ESP32/1.0.0\r\n") +
                   String("Accept-Encoding: identity\r\n") +
                   String("Accept: text/html\r\n\r\n");

    if (!clientsecure.connect(host.c_str(), 443)) {
        Serial.println("Connection failed");
        return false;
    }
    clientsecure.print(resp);

    while (clientsecure.connected()) {  // read the header
        String line = clientsecure.readStringUntil('\n');
        line+="\n";
    //      if(vs1053_info) vs1053_info(line.c_str());
        if (line == "\r\n") break;
    }

    String tkkFunc;
    char ch;
    do {  // search for TKK
        tkkFunc = "";
        clientsecure.readBytes(&ch, 1);
        if (ch != 'T') continue;
        tkkFunc += String(ch);
        clientsecure.readBytes(&ch, 1);
        if (ch != 'K') continue;
        tkkFunc += String(ch);
        clientsecure.readBytes(&ch, 1);
        if (ch != 'K') continue;
        tkkFunc += String(ch);
    } while(tkkFunc.length() < 3);
    tkkFunc +=  clientsecure.readStringUntil('}');
    int head = tkkFunc.indexOf("3d") + 2;
    int tail = tkkFunc.indexOf(";", head);
    char* buf;
    unsigned long a = strtoul(tkkFunc.substring(head, tail).c_str(), &buf, 10);
    head = tkkFunc.indexOf("3d", tail) + 2;
    tail = tkkFunc.indexOf(";", head);
    unsigned long b = strtoul(tkkFunc.substring(head, tail).c_str(), &buf, 10);
    head = tkkFunc.indexOf("return ", tail) + 7;
    tail = tkkFunc.indexOf("+", head);
    String key1 = tkkFunc.substring(head, tail);
    String key2  = String(a+b);
    long long int a1, b1;
    a1 = b1 = strtoll(key1.c_str(), NULL, 10);
    int f;
    int len = speech.length();
    String para;
    for (f = 0; f < len; f++) {
        a1 += speech[f];
        a1 = XL(a1, "+-a^+6");
     }
     a1 = XL(a1, "+-3^+b+-f");
     a1 = a1 ^ (strtoll(key2.c_str(), NULL, 10));
     if (0 > a1) {
       a1 = (a1 & 2147483647) + 2147483648;
     }
     a1 = a1 % 1000000;
     String token=String(lltoa(a1, 10)) + '.' + String(lltoa(a1 ^ b1, 10));
     int i,j;
     const char* t = speech.c_str();
     for(i=0,j=0;i<strlen(t);i++) {
       if (t[i] < 0x80 || t[i] > 0xbf) {
         j++;
       }
     }
     String tts= String("https://") + host + path +
                        "?ie=UTF-8&q=" + urlencode(speech) +
                        "&tl=" + lang +
                        "&textlen=" + String(j) +
                        "&tk=" + token +
                        "&total=1&idx=0&client=t&prev=input&ttsspeed=1";

    clientsecure.stop();  clientsecure.flush();

    resp=   String("GET ") + tts + String("HTTP/1.1\r\n") +
            String("Host: ") + host + String("\r\n") +
            String("Connection: close\r\n\r\n");

    if (!clientsecure.connect(host.c_str(), 443)) {
        Serial.println("Connection failed");
        return false;
    }
    clientsecure.print(resp);

    while (clientsecure.connected()) {
        String line = clientsecure.readStringUntil('\n');
        line+="\n";
    //      if(vs1053_info) vs1053_info(line.c_str());
        if (line == "\r\n") break;
    }
    uint8_t mp3buff[32];
    startSong();
    while(clientsecure.available() > 0) {
        uint8_t bytesread = clientsecure.readBytes(mp3buff, 32);
        sdi_send_buffer(mp3buff, bytesread);
    }
    clientsecure.stop();  clientsecure.flush();
    if(vs1053_eof_speech) vs1053_eof_speech(speech.c_str());
    return true;
}
//---------------------------------------------------------------------------------------
long long int VS1053::XL (long long int a, const char* b) {
  int len = strlen(b);
  for (int c = 0; c < len - 2; c += 3) {
    int  d = (long long int)b[c + 2];
    d = d >= 97 ? d - 87 : d - 48;
    d = (b[c + 1] == '+' ? a >> d : a << d);
    a = b[c] == '+' ? (a + d) & 4294967295 : a ^ d;
  }
  return a;
}
//---------------------------------------------------------------------------------------
char* VS1053::lltoa(long long val, int base){

    static char buf[64] = {0};
    static char chn=0;
    int i = 62;
    int sign = (val < 0);
    if(sign) val = -val;

    if(val == 0) return &chn;

    for(; val && i ; --i, val /= base) {
        buf[i] = "0123456789abcdef"[val % base];
    }

    if(sign) {
        buf[i--] = '-';
    }
    return &buf[i+1];
}
//---------------------------------------------------------------------------------------
String VS1053::urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    for (int i =0; i < str.length(); i++){
        c=str.charAt(i);
        if (c == ' ') encodedString+= '+';
        else if (isalnum(c)) encodedString+=c;
        else{
            code1=(c & 0xf)+'0';
            if ((c & 0xf) >9) code1=(c & 0xf) - 10 + 'A';
            c=(c>>4)&0xf;
            code0=c+'0';
            if (c > 9) code0=c - 10 + 'A';
            encodedString+='%';
            encodedString+=code0;
            encodedString+=code1;
        }
    }
    return encodedString;
}
/*
//---------------------------------------------------------------------------------------
void VS1053::setSystemFlagGroup(EventGroupHandle_t eventGroup)
{
    m_SystemFlagGroup = eventGroup;
}
*/