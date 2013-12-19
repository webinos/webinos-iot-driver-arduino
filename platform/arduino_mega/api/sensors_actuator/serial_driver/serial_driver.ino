#include <SD.h>
#include "Util.h"

#define CONNECTED      "con"
#define IDLE           "ele"
#define START          "str"
#define STOP           "stp"
#define CONFIGURE      "cfg"
#define SET            "set"

boolean sd_ready = false;
#define  CONFIG_FILE "config.txt"
int    num_elements=0;
char*  board_id;
char sep = '$';

#define  MAX_NUM_ELEMENTS     10
#define  DFLT_ELEMENT_RATE    1000
#define  DFLT_ELEMENT_ACTIVE  false

int num_message = 0;

typedef struct {
    char* id;
    bool sa;
    bool ad;
    int pin;
    bool active;
    bool mode;    // 0=valuechange, 1=fixedinterval
    long rate;
    int lastValue;
    long lastConnectionTime;
} IOElement;

IOElement * elements[MAX_NUM_ELEMENTS];

void getInfoFromSD(boolean check_for_elements){
    boolean first_element = true;
    boolean ignore = false;
    
    // see if the card is present and can be initialized:
    if (!sd_ready && !SD.begin(chipSelect)) {
        //Serial.println("Card failed, or not present");
        // don't do anything more:
        return;
    }
    else{
        //Serial.println("card initialized.");
        sd_ready = true;
    }
    
    // open the file. note that only one file can be open at a time,
    // so you have to close this one before opening another.
    File configFile = SD.open(CONFIG_FILE);
    if (configFile) {
        String s;
        char c;
         
        while (configFile.available()) {
            c = configFile.read();
            if(c == '#' ){
                ignore = true;
                s="";
            }
            else if(!ignore){
                if(s.length()==7 && !s.startsWith("BOARDID") && !s.startsWith("MACADDR") && !s.startsWith("BRDIPAD") && !s.startsWith("BRDPORT") && !s.startsWith("PZPIPAD") && !s.startsWith("PZPPORT") && !s.startsWith("ELEMENT")){
                    s = "";
                    ignore = true;
                }
                else
                    if(c!=' ' && c !='\t' && c!='\r')
                        s += c;
            }
          
            if(c == '\n'){
                ignore = false;
                if(!check_for_elements && s.startsWith("BOARDID")){  // BOARD ID
                    String tmp = s.substring(7);
                    char * buf = (char*) malloc(sizeof(char)*tmp.length());
                    for(int i=0; i<tmp.length();i++)
                        buf[i] = tmp.charAt(i);
                    buf[tmp.length()-1] = '\0';
                    board_id = buf;
                }
//                else if(!check_for_elements && s.startsWith("BRDIPAD")){  // BOARD IP ADDRESS
//                    board_ip = strIp2byteVect(s.substring(7));
//                }
//                else if(!check_for_elements && s.startsWith("BRDPORT")){  // BOARD PORT 
//                    board_port = s.substring(7).toInt();
//                }
//                else if(!check_for_elements && s.startsWith("PZPIPAD")){  // PZP IP ADDRESS
//                    pzp_ip = strIp2byteVect(s.substring(7));
//                }
//                else if(!check_for_elements && s.startsWith("PZPPORT")){  // PZP PORT
//                    pzp_port = s.substring(7).toInt();
//                }
//                else if(!check_for_elements && s.startsWith("MACADDR")){  // MAC ADDRESS
//                    String tmp = s.substring(7);
//                    mac = strMac2byteVect(tmp);
//                }
                else if(check_for_elements && num_elements < MAX_NUM_ELEMENTS && s.startsWith("ELEMENT")){
                    String tmp = s.substring(7);
                    String field;
                    int counter=0;
                    int sep_pos = 0;
                    if(first_element){
                        first_element=false;
                        Serial.print("[");
                    }
                    else{
                        Serial.print(",");
                    }
                    
                    bool isActuator = false;
                    for(int i=0; i<tmp.length(); i++){
                        if(tmp.charAt(i) == sep || tmp.charAt(i) == '\n'){
                            field = tmp.substring(sep_pos,i);
                            
                            if(counter == 0){  // ELEMENT ID
                                elements[num_elements] = new IOElement();
                                Serial.print("{\"id\":\"");
                                Serial.print(board_id);
                                Serial.print("_");
                                Serial.print(field);
                                Serial.print("\", \"element\":{");
                                
                                int len = strlen(board_id) + 1 + field.length() +1 ;  // BOARD_ID + _ + id + \0
                                char * buf = (char*) malloc(sizeof(char)*(len));
                                strcpy(buf,board_id);
                                strcat(buf,"_");
                                for(int i=strlen(board_id)+1,j=0; i<len;i++,j++)
                                    buf[i] = field.charAt(j);
                                buf[len] = '\0';                                
                                elements[num_elements]->id = buf;
                                elements[num_elements]->active = DFLT_ELEMENT_ACTIVE;
                                elements[num_elements]->rate = DFLT_ELEMENT_RATE;
                            }
                            else if(counter == 1){  // ELEMENT SA
                                Serial.print("\"sa\":\"");
                                Serial.print(field);
                                Serial.print("\",");
                                elements[num_elements]->sa = !field.equals("0");
                                if(elements[num_elements]->sa == 1)
                                    isActuator = 1;
                            }
                            else if(counter == 2){  // ELEMENT AD
                                elements[num_elements]->ad = !field.equals("0");
                            }
                            else if(counter == 3){  // ELEMENT PIN
                                int pin = field.toInt();
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
                                    Serial.print("\"type\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                                else{ // SENSOR MAXIMUMRANGE
                                    Serial.print("\"maximumRange\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 5){  
                                if(isActuator){  // ACTUATOR RANGE
                                    Serial.print("\"range\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                                else{ // SENSOR MINDELAY
                                    Serial.print("\"minDelay\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 6){
                                if(isActuator){ // ACTUATOR VENDOR 
                                    Serial.print("\"vendor\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                                else{ // SENSOR POWER
                                    Serial.print("\"power\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 7){
                                if(isActuator){ // ACTUATOR VERSION
                                    Serial.print("\"version\":\"");
                                    Serial.print(field);
                                    Serial.print("\"}}");
                                }
                                else{ // SENSOR RESOLUTION
                                    Serial.print("\"resolution\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 8){
                                if(!isActuator){ // SENSOR TYPE
                                    Serial.print("\"type\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 9){  // SENSOR VENDOR
                                if(!isActuator){
                                    Serial.print("\"vendor\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
                                }
                            }
                            else if(counter == 10){  // SENSOR VERSION
                                if(!isActuator){
                                    Serial.print("\"version\":\"");
                                    Serial.print(field);
                                    Serial.print("\",");
//                                    Serial.print("\"}}");
                                }
                            }                           
                            else if(counter == 11){  // SENSOR FUNCTION
                                if(!isActuator){
                                    Serial.print("\"convfunc\":\"");
                                    Serial.print(field);
                                    Serial.print("\"}}");                                    
                                }
                            }
                            
                            counter++;
                            sep_pos = i+1;
                        }
                        field += tmp.charAt(i);                    
                    }
                    num_elements++;
                }
                s = "";
            }
        }
        if(check_for_elements){
              Serial.print("]");
        }
        configFile.close();
    }
    else {
        //Serial.println("error opening configuration file");
    }
}


void connect_to_pzp(){
    Serial.print("<{\"cmd\":\"con\", \"id\":\"");
    Serial.print(board_id);
    Serial.println("\",\"protocol\":\"serial\",\"name\":\"ARDUINO_MEGA\"}>");
}

void send_ack(char* cmd, char* eid, char* dat){
    Serial.println();
    Serial.print("<{\"cmd\":\"");
    Serial.print(cmd);
    Serial.print("\",\"id\":\"");
    Serial.print(eid);
    Serial.print("\",\"dat\":\"");
    Serial.print(dat);
    Serial.print("\",\"num\":\"");
    Serial.print(num_message++);
    Serial.println("\"}>");
    Serial.println();
    delay(100);
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
        if(value == 0){
            digitalWrite(pin,LOW);
        }
        else if(value == 1){
            digitalWrite(pin,HIGH);
        }
    }
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
    Serial.begin(115200);
    
    getInfoFromSD(false);
    
    while(board_id == NULL){
         Serial.println("Please disconnect and reconnect the board");
         err_SD();
         send_ack("err","-1","sd");
         getInfoFromSD(false);
    }
    
    delay(1000);
    connect_to_pzp();

}

String s = "";
char cmd[4];
char id[4];
char dat[30];
bool prevIsCmd = false;
String configuration_params;


void loop(){ 
  
    if(Serial.available()>0){
        char c = (char) Serial.read();
        if(c == '\n'){
            prevIsCmd = true;
            //cmd=con&eid=000&dat=ack
            int i=0,j=0,z=0;
            bool finish = false;
            
            int index = s.indexOf("=") +1 ;
            cmd[i++] = s.charAt(index);
            cmd[i++] = s.charAt(index+1);
            cmd[i++] = s.charAt(index+2);
            
            index = s.indexOf("=",index) +1 ;
            id[j++] = s.charAt(index);
            id[j++] = s.charAt(index+1);
            id[j++] = s.charAt(index+2);
            
            for(index=s.indexOf("=",index)+1; index<s.length();index++){
                if(s.charAt(index) != '\n')
                    dat[z++] = s.charAt(index);
                else
                    break;
            }
            
            cmd[i] = '\0';
            id[j] = '\0';
            dat[z] = '\0';   
            s = "";
        }
        else
            s += c;

        int len = strlen(board_id) + 1 + strlen(id);  // BOARD_ID + _ + pin
        char eid[len+1];
        strcpy(eid,board_id);
        strcat(eid,"_");
        strcat(eid,id);
        strcat(eid,'\0'); 
        if(strcmp(cmd, CONNECTED) == 0){
            //Serial.print(ELEMENTS_RES);
            Serial.print("<{\"id\":\"");
            Serial.print(board_id);
            Serial.print("\",\"cmd\":\"ele\",\"elements\":");
            getInfoFromSD(true);
            Serial.println("}>");
        }
        else if(strcmp(cmd, START) == 0){
            for(int i=0; i<num_elements; i++){
                if(strcmp(elements[i]->id, eid) == 0){
                    elements[i]->active = true;
                    send_ack(cmd,eid,dat);
                    break;
                 }
             }
        }
        else if(strcmp(cmd, STOP) == 0){
            //stopping element eid
            for(int i=0; i<num_elements; i++){
                if(strcmp(elements[i]->id, eid) == 0)
                    elements[i]->active = false;
            }
            send_ack(cmd,eid,dat);
        }
        else if(strcmp(cmd, CONFIGURE) == 0){    
            //configuring element eid
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
                    else //valuechange
                        elements[i]->mode = 0;
                }                      
            }
            send_ack(cmd,eid,dat);  
            delay(500);    
        }
        else if(strcmp(cmd, SET) == 0){
            //setting element eid
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
            send_ack(cmd,eid,dat);
        }
        else
            ;
        
        for(int i=0;i<4;i++)
            cmd[i] = '\0';
    }

    for(int i=0; i<num_elements; i++){
        if(elements[i]->active){
            int val = getValueFromSensor(elements[i]->ad, elements[i]->pin);
            if(elements[i]->mode == 1 && millis() - elements[i]->lastConnectionTime > elements[i]->rate){
                char cval[4];
                itoa(val,cval,10);
                send_ack("val",elements[i]->id, cval);
                elements[i]->lastConnectionTime = millis();
            }
            else if(elements[i]->mode == 0){
                if(val == elements[i]->lastValue){
                    char cval[4];
                    itoa(val,cval,10);
                    send_ack("val",elements[i]->id, cval);
                }
            }
            elements[i]->lastValue = val;
        }
    }
}
