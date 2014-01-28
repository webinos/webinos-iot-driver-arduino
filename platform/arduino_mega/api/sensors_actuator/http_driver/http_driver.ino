/*******************************************************************************
 *  Code contributed to the webinos project
 * 
 * Licensed under the Apache License, Version 2.0 (the 'License');
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *  
 *     http://www.apache.org/licenses/LICENSE-2.0
 *  
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an 'AS IS' BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * Author: Giuseppe La Torre - University of Catania
 * 
 ******************************************************************************/

#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include "Util.h"


#define  CONFIG_FILE          "config.txt"
#define  DFLT_BOARD_IP        192,168,1,120
#define  DFLT_BOARD_PORT      80
#define  MAX_NUM_ELEMENTS     10
#define  DFLT_ELEMENT_RATE    1000
#define  DFLT_ELEMENT_ACTIVE  false

typedef struct {
    char* id;
    bool sa;      // 0=sensor, 1=actuator
    bool ad;      // 0=analog, 1=digital
    int pin;
    bool active;
    bool mode;    // 0=valuechange, 1=fixedinterval
    long rate;
    int lastValue;
    long lastConnectionTime;
} IOElement;

IOElement * elements[MAX_NUM_ELEMENTS];
byte MAC[] = {  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xAC };
byte* mac;
byte* pzp_ip;
byte* board_ip;
IPAddress BOARD_IP(DFLT_BOARD_IP);
EthernetClient client;
EthernetServer server(DFLT_BOARD_PORT);
int pzp_port;
int board_port;

char*  board_id;
int    num_elements=0;
boolean boardconnected = false;
boolean sd_ready = false;
boolean elements_ready = false;
char sep = '$';
char* elementsBuffer;
unsigned long lastAliveReplyTime = 0;
int pzpTimeout = 5000;

void sayHello(){
    client.print(F("GET /newboard?jsondata={\"id\":\""));
    client.print(board_id);
    client.print(F("\",\"protocol\":\"HTTP\",\"name\":\"ARDUINO_MEGA\",\"ip\":\""));
    client.print(Ethernet.localIP());
    client.println(F("\",\"port\":\"80\"} HTTP/1.0"));
}


bool startsWith(char * vet, char str[]){
  for(int i=0; i<strlen(str);i++){
    if(vet[i] != str[i])
      return false;
  }
  return true;
}

char * substr1(char * str, int start){
  int len = strlen(str);
  char* s = (char*) malloc(sizeof(char)*(len-start+1));
  for(int j=0,i=start; i<len; i++,j++)
    s[j] = str[i];
  s[len-start] = '\0';
  return s;
}

char * substr2(char * str, int from, int to){
  int len = to-from+1;
  char* s = (char*) malloc(sizeof(char)*(len+1));
  for(int j=0,i=from; i<=to; i++,j++)
    s[j] = str[i];
  s[len] = '\0';
  return s;
}

char* readFromSD(){
    pinMode(chipSelect, OUTPUT);
    if (!sd_ready && !SD.begin(chipSelect)) {
      Serial.println(F("Card failed, or not present"));
      // don't do anything more:
    }
    else{
      Serial.println(F("card initialized."));
      sd_ready = true;
    }
    File configFile = SD.open(CONFIG_FILE);
    if (configFile) {
      String s;
      while (configFile.available()) {
        s += (char)configFile.read();
      }

      char*buf = (char*) malloc(sizeof(char)*(s.length()+1));
      strcpy(buf, s.c_str());
      buf[s.length()] = '\0';
      configFile.close();
      return buf;
    }
    else {
      Serial.println(F("error opening configuration file"));
      configFile.close();
      return NULL;
    }
}

void getInfoFromSD(){
    boolean first_element = true;
    boolean ignore = false;
    char * sd_buf = readFromSD();
    if (sd_buf != NULL) {
        char * elementsBuf = (char*) malloc(sizeof(char)*(1500));
        int buf_size = 100;
        int count = 0;
        char* s = (char*) malloc(sizeof(char)*(buf_size+1));
        char c;
        
        client.stop();

        if(!client.connected())
          client = server.available();

        for(int y=0;y<strlen(sd_buf);y++){
            c = sd_buf[y];   
            if(c == '#' ){
                ignore = true;
                count = 0;
                s[count] = '\0';
            }
            else if(!ignore){
                  if(strlen(s)==7 && !startsWith(s,"BOARDID") && !startsWith(s,"MACADDR") && !startsWith(s,"BRDIPAD") && !startsWith(s,"BRDPORT") && !startsWith(s,"PZPIPAD") && !startsWith(s,"PZPPORT") && !startsWith(s,"ELEMENT")){
                    count = 0;
                    s[count] = '\0';
                    ignore = true;
                }
                else
                    if(c!=' ' && c !='\t' && c!='\r')
                        s[count++] = c;
                        s[count] = '\0';
            }
          
            if(c == '\n'){
                ignore = false;
                if(startsWith(s,"BOARDID")){  // BOARD ID
                    char * tmp = substr1(s,7);
                    char * buf = (char*) malloc(sizeof(char)*strlen(tmp));
                    for(int i=0; i<strlen(tmp);i++)
                        buf[i] = tmp[i];
                    buf[strlen(tmp)-1] = '\0';
                    board_id = buf;
                }
                else if(startsWith(s,"BRDIPAD")){  // BOARD IP ADDRESS
                    board_ip = strIp2byteVect(substr1(s,7));
                }
                else if(startsWith(s,"BRDPORT")){  // BOARD PORT 
                    board_port = atoi(substr1(s,7));
                }
                else if(startsWith(s,"PZPIPAD")){  // PZP IP ADDRESS
                    pzp_ip = strIp2byteVect(substr1(s,7));
                }
                else if(startsWith(s, "PZPPORT")){  // PZP PORT
                    pzp_port = atoi(substr1(s,7));
                }
                else if(startsWith(s, "MACADDR")){  // MAC ADDRESS
                    mac = strMac2byteVect(substr1(s,7));
                }
                else if(num_elements < MAX_NUM_ELEMENTS && startsWith(s,"ELEMENT")){
                    char * tmp = substr1(s,7);
                    char * field;
                    int counter=0;
                    int sep_pos = 0;
                    if(first_element){
                        first_element=false;
                        strcpy(elementsBuf,"[");
                    }
                    else{
                        strcat(elementsBuf,",");
                    }
                    
                    bool isActuator = false;
                      for(int i=0; i<strlen(tmp); i++){
                          if(tmp[i] == sep || tmp[i] == '\n'){
                            field = substr2(tmp, sep_pos,i-1);
                            
                            if(counter == 0){  // ELEMENT ID
                                elements[num_elements] = new IOElement();
                                strcat(elementsBuf, "{\"id\":\"");
                                strcat(elementsBuf, board_id);
                                strcat(elementsBuf,"_");
                                strcat(elementsBuf,field);
                                strcat(elementsBuf,"\", \"element\":{");

                                int len = strlen(board_id) + 1 + strlen(field) +1 ;
                                char * buf = (char*) malloc(sizeof(char)*(len));
                                strcpy(buf,board_id);
                                strcat(buf,"_");
                                for(int i=strlen(board_id)+1,j=0; i<len;i++,j++){
                                    buf[i] = field[j];
                                }
                                buf[len] = '\0';                                
                                elements[num_elements]->id = buf;
                                elements[num_elements]->active = DFLT_ELEMENT_ACTIVE;
                                elements[num_elements]->rate = DFLT_ELEMENT_RATE;
                            }
                            else if(counter == 1){  // ELEMENT SA
                                strcat(elementsBuf, "\"sa\":\"");
                                strcat(elementsBuf,field);
                                strcat(elementsBuf,"\",");
                                elements[num_elements]->sa = field[0]=='0' ? 0 : 1;
                                if(elements[num_elements]->sa == 1)
                                    isActuator = 1;
                            }
                            else if(counter == 2){  // ELEMENT AD
                                elements[num_elements]->ad = field[0]=='0' ? 0 : 1;
                            }
                            else if(counter == 3){  // ELEMENT PIN
                                int pin = atoi(field);
                                elements[num_elements]->pin = pin;
                                if(elements[num_elements]->ad == 0){
                                    pinMode(pin,INPUT);
                                }
                                else{
                                    pinMode(pin,OUTPUT);
                                }
                            }
                            else if(counter == 4){
                                if(isActuator){  // ACTUATOR TYPE
                                    strcat(elementsBuf, "\"type\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                                else{ // SENSOR MAXIMUMRANGE
                                    strcat(elementsBuf,"\"maximumRange\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 5){  
                                if(isActuator){  // ACTUATOR RANGE
                                    strcat(elementsBuf,"\"range\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                                else{ // SENSOR MINDELAY
                                    strcat(elementsBuf,"\"minDelay\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 6){
                                if(isActuator){ // ACTUATOR VENDOR 
                                    strcat(elementsBuf,"\"vendor\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                                else{ // SENSOR POWER
                                    strcat(elementsBuf,"\"power\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 7){
                                if(isActuator){ // ACTUATOR VERSION
                                    strcat(elementsBuf,"\"version\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\"}}");
                                }
                                else{ // SENSOR RESOLUTION
                                    strcat(elementsBuf,"\"resolution\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 8){
                                if(!isActuator){ // SENSOR TYPE
                                    strcat(elementsBuf,"\"type\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 9){  // SENSOR VENDOR
                                if(!isActuator){
                                    strcat(elementsBuf,"\"vendor\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 10){  // SENSOR VERSION
                                if(!isActuator){
                                    strcat(elementsBuf,"\"version\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\",");
                                }
                            }
                            else if(counter == 11){  // SENSOR FUNCTION
                                if(!isActuator){
                                    strcat(elementsBuf,"\"convfunc\":\"");
                                    strcat(elementsBuf,field);
                                    strcat(elementsBuf,"\"}}");                                    
                                }
                            }
                            counter++;
                            sep_pos = i+1;
                        }
                        field += tmp[i];
                    }
                    num_elements++;
                }
                count = 0;
                s[count] = '\0';
            }
        }
        
        strcat(elementsBuf,"]\0");
        
        free(s);
        free(sd_buf);
        elementsBuffer = (char*) malloc(strlen(elementsBuf+1));
        strcpy(elementsBuffer, elementsBuf);
        elementsBuffer[strlen(elementsBuf)] = '\0';
        free(elementsBuf);
    }
    else {
        Serial.println(F("error opening configuration file"));   
    }
}

int getValueFromSensor(bool ad, int pin){  
    int value = -1;
    if(ad == 0){ //analog sensor
        value = analogRead(pin);
    }
    else{ //digital sensor
        value = digitalRead(pin);
    }
    return value;
}

void setValueToActuator(bool ad, int pin, int value){
    if(ad == 0){ //analog actuator
        analogWrite(pin, value);
    }
    else{ //digital actuator
        if(value == 0)
            digitalWrite(pin,LOW);
        else if(value == 1)
            digitalWrite(pin,HIGH);
    }
}

void sendDataToPZP(int id_ele, bool check_value_is_changed){
    bool senddata = true;
    int val = getValueFromSensor(elements[id_ele]->ad, elements[id_ele]->pin);
    if(check_value_is_changed == true){
        if(val == elements[id_ele]->lastValue){
            senddata = false;
        }
    }
    
    client.stop();
    
    if (senddata && client.connect(pzp_ip, pzp_port)) {
        client.print("POST /sensor?id=");
        client.print(elements[id_ele]->id);
        client.print("&data=");
        client.print(val);
        client.println(" HTTP/1.0");
        client.println();
        elements[id_ele]->lastConnectionTime = millis();
        delay(10);
        client.stop();
    }
    elements[id_ele]->lastValue = val;
}


void err_SD(){
    digitalWrite(13,HIGH);
    delay(800);
    digitalWrite(13,LOW);
    delay(300);
    digitalWrite(13,HIGH);
    delay(800);
    digitalWrite(13,LOW);
    delay(1000);
}


void setup(){
    Serial.begin(9600);
    pinMode(13, OUTPUT);
    sd_ready = false;
    Serial.println("Start");
    //sd_buf = readFromSD();
    getInfoFromSD();
    
    while(board_id == NULL){
        Serial.println(F("Please disconnect and reconnect the board"));
        err_SD();
        delay(1000);
        //sd_buf = readFromSD();
        getInfoFromSD();
    }
    
    if(mac == NULL){
        Serial.println(F("Using default MAC"));
        mac = MAC;
    }  
    
    if(board_ip == NULL){  // board ip is not defined in the configuration file
        Serial.println("Trying to get an IP address using DHCP");
        if (Ethernet.begin(mac) == 0){
            Serial.println("Failed to configure Ethernet using DHCP. Using default IP");
            Ethernet.begin(mac, BOARD_IP);
        }
    }
    else
        Ethernet.begin(mac, board_ip);
    
    Serial.print("My IP address: ");
    Serial.println(Ethernet.localIP());
}

void loop(){
    if(!boardconnected){
        Serial.println(F("Try connecting to PZP"));
        Serial.println(pzp_port);

        if (client.connect(pzp_ip, pzp_port)) {
            sayHello();
            client.println();
            delay(1000);
            
            String s;
            bool nextisbody = false;   
            char vet[19];
            int i=0;
            int num_spaces=0;
            while(client.available() > 0){
                char c = client.read();
                if(num_spaces == 4){
                    vet[i++] = c;
                    if(i==19){
                        vet[i-1] = '\0';
                        String s = vet;
                        if(s == "{\"ack\":\"newboard\"}"){
                            Serial.println(F("new board"));
                            boardconnected=true;
                            for(int i=0; i<num_elements; i++){
                                elements[i]->active = false;
                            }
                        }
                        else{
                            Serial.println(F("Connection Error"));
                        }            
                        break;
                    }
                }
                else{
                    if(c == '\n' || c == '\r'){
                        num_spaces++;
                    }
                    else
                        num_spaces=0;
                }
            }
            client.stop();
        }
        else {
            Serial.println(F("PZP unavailable"));
            delay(2000);
        }
    }
    else{ // boardisconnected == true
        EthernetClient client = server.available();
        if (client) {
            // an http request ends with a blank line
            boolean currentLineIsBlank = true;
            char cmd[4];
            char id[4];
            char dat[30];
            int counter=1;
            int i=0,j=0,z=0;
            bool finish = false;

            while (client.connected()) {    
                if (client.available()) {
                    char c = client.read();               
                    if(counter>10 && counter<14)
                        cmd[i++] = c;         
                    if(counter>18 && counter<22)
                        id[j++] = c;
                    if(counter > 26){
                        dat[z++] = c;
                        if(c == '\ ')
                            finish=true;
                    }
                    if(finish){
                        cmd[i] = '\0';
                        id[j] = '\0';
                        dat[z-1] = '\0';
                    
                        Serial.println(cmd);
                        Serial.println(id);
                        Serial.println(dat);
                        client.println(F("HTTP/1.1 200 OK"));
                        client.println(F("Content-Type: application/json"));
                        client.println(F("Connnection: keep-alive"));
                        client.println();
                    
                        int len = strlen(board_id) + 1 + strlen(id);  // BOARD_ID + _ + id
                        char eid[len+1];
                        strcpy(eid,board_id);
                        strcat(eid,"_");
                        strcat(eid,id);
                        strcat(eid,'\0');
  
                        if(strcmp(cmd,"ele")==0){
                            client.print("{\"id\":\"");
                            client.print(board_id);
                            client.print("\",\"cmd\":\"ele\",\"elements\":");
                            client.print(elementsBuffer);
                            client.println("}");
                            elements_ready = true;
                            lastAliveReplyTime = millis();
                        }
                        else if(strcmp(cmd,"get")==0){
                            int pin = -1;
                            bool ad;
                            for(int i=0; i<num_elements;i++){
                                if(strcmp(elements[i]->id, eid)==0){
                                    pin = elements[i]->pin;
                                    ad = elements[i]->ad;
                                    break;
                                }
                            }
                            int value = getValueFromSensor(ad,pin);
                            client.print("{\"cmd\":\"get\",\"id\":\"");
                            client.print(board_id);
                            client.print("_");
                            client.print(id);
                            client.print("\",\"dat\":\"");
                            client.print(value);
                            client.println("\"}");
                        }
                        else if(strcmp(cmd,"str")==0 || strcmp(cmd,"stp")==0){                                     
                            if(strcmp(cmd,"str")==0){
                                client.print("{\"cmd\":\"str\",");                                  
                                for(int i=0; i<num_elements; i++){
                                    if(strcmp(elements[i]->id, eid) == 0){
//                                        Serial.print("starting ");
//                                        Serial.println(elements[i]->id);
                                        elements[i]->active = true;
                                        if(strcmp(dat,"fix")==0){
                                            elements[i]->mode = 1;
                                        }
                                        else{ //vch
                                            elements[i]->mode = 0;
                                        }
                                    }
                                }
                            }
                            else{ 
                                client.print("{\"cmd\":\"stp\",");
                                for(int i=0; i<num_elements; i++){
                                    if(strcmp(elements[i]->id, eid) == 0){
//                                        Serial.print("stopping ");
//                                        Serial.println(elements[i]->id);
                                        elements[i]->active = false;
                                    }
                                }
                            }
                            client.print("\"id\":\"");
                            client.print(eid);
                            client.println("\"}");
                        }
                        else if(strcmp(cmd,"alv")==0){
                            lastAliveReplyTime = millis();
                            Serial.println(lastAliveReplyTime);
                            client.print("{\"cmd\":\"alv\",\"id\":\"");
                            client.print(board_id);
                            client.println("\"}");
                        }
                        else if(strcmp(cmd,"cfg")==0){
                            String tmp = dat;
                            char cfg_sep = ':';
                            int last_sep_pos=0;
                            String s;
                            int sep_pos = tmp.indexOf(cfg_sep);
                            s = tmp.substring(0, sep_pos);                          
                            last_sep_pos = sep_pos + 1;
                            sep_pos = tmp.indexOf(cfg_sep, last_sep_pos);
                            s = tmp.substring(last_sep_pos, sep_pos);
                            for(int i=0; i<num_elements; i++)
                                if(strcmp(elements[i]->id, eid) == 0)
                                    elements[i]->rate = s.toInt();                                                    
                            last_sep_pos = sep_pos + 1;
                            s = tmp.substring(last_sep_pos);
                            for(int i=0; i<num_elements; i++){
                                if(strcmp(elements[i]->id, eid) == 0){
                                    if(s.equals("fix"))
                                        elements[i]->mode = 1;
                                    else //vch
                                        elements[i]->mode = 0;
                                 }                      
                            }
                            client.print("{\"cmd\":\"cfg\",");
                            client.print("\"id\":\"");
                            client.print(eid);
                            client.println("\"}");
                        }
                        else if(strcmp(cmd,"set")==0){
                            int pin = -1;
                            bool ad;
                            for(int i=0; i<num_elements;i++){
                                if(strcmp(elements[i]->id, eid)==0){
                                    pin = elements[i]->pin;
                                    ad = elements[i]->ad;
                                    String sdat = dat;
                                    setValueToActuator(ad, pin, sdat.toInt());
                                    break;
                                }
                            }
                            
                            client.print("{\"cmd\":\"set\",\"id\":\"");
                            client.print(board_id);
                            client.print("_");
                            client.print(id);
//                            client.print("\",\"dat\":\"");
//                            client.print(dat);
                            client.println("\"}");
                        }
                        break;
                    }
                    counter++;
                }
            }
            // give the web browser time to receive the data
            delay(10);
            client.stop();
        }  
        
        if(elements_ready){
            for(int i=0; i<num_elements; i++){
                if(elements[i]->active){
                    if(elements[i]->mode == 1 && millis() - elements[i]->lastConnectionTime > elements[i]->rate){
                        sendDataToPZP(i,0);
                    }
                    else if(elements[i]->mode == 0){
                        sendDataToPZP(i,1);
                    }
                }
            }
        }
    }

    if(elements_ready){
      if(millis() - lastAliveReplyTime > pzpTimeout){
        Serial.println(F("Timeout exceeded"));
        boardconnected = false;
        elements_ready = false;
        client.stop();
      }
    }
}
