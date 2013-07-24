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
 * Author: Giuseppe La Torre - giuseppe.latorre@dieei.unict.it
 * 
 ******************************************************************************/

 (function () {
    'use strict';

    var serialport_module = require('serialport');
    var serialPort = serialport_module.SerialPort;

    var sb;
    try{
        var Sandbox = require("sandbox");
        sb = new Sandbox();    
    }
    catch(err){
        console.log("serialDriver.js : " + err);
    }
    

    var path = require("path");
    var fs = require("fs");

    var driverId = null;
    var registerFunc = null;
    var removeFunction = null;
    var callbackFunc = null;

    var START_LISTENING_CMD         = "str";
    var STOP_LISTENING_CMD          = "stp";
    var GET_ELEMENTS_CMD            = "ele";
    var CONFIGURE_CMD               = "cfg";
    var GET_SENSOR_VALUE_CMD        = "get";
    var SET_ACTUATOR_VALUE_CMD      = "set";
    var VALUECHANGE_MODE            = "vch";
    var FIXEDINTERVAL_MODE          = "fix";
    var NO_VALUE                    = "000";

    var elementsList = new Array();
    var handlers = {};

    var SERIAL_PORT = function(s_name, s_port, s_rate){
        var incoming_data = "";
        var serial = new serialPort(s_port, {baudrate: s_rate}, false);
        var intervalId = setInterval(waiting_for_serial,2000);
        var boards = new Array();

        console.log("Init Serial Port - [NAME] : " + s_name + ", [PATH] : " + s_port + ", [RATE] : " + s_rate);
        
        function isAlreadyRegistered(nativeid){
            for(var i in elementsList)
                if(elementsList[i].id === nativeid)
                    return true;
            return false;
        }

        function waiting_for_serial(){
            //console.log("Waiting for "+ s_port);
            serialport_module.list(function (err, ports) {
                ports.forEach(function(port) {
                    if(port.comName == s_port){
                        clearInterval(intervalId);
                        serial.open(function () {
                            serial.on('close', function (err) {
                                console.log("Serial port ["+s_port+"] was closed");

                                //handle board disconnection
                                if(elementsList){
                                    for(var i in elementsList){
                                        removeFunction(i, elementsList[i].element.sa);
                                    }
                                    elementsList = [];
                                }
                                //start listening for incoming boards
                                intervalId = setInterval(waiting_for_serial,2000);
                            });

                            serial.on('error', function (err) {
                                if(err.path == s_port){
                                    //console.log("Serial port ["+port+"] is not ready. Err code : "+err.code);
                                    setTimeout(init_serial,2000);
                                }
                            });
                            start_serial();
                        });
                      }
                });
            });       
        }

        function start_serial(){
            serial.on( "data", function( chunk ) {
                incoming_data += chunk.toString().replace(/\n/g,'');
                //console.log(incoming_data);
                try{
                    var start = incoming_data.indexOf("<");
                    var stop = incoming_data.indexOf(">", start);
                    if(start != -1 && stop != -1){
                        var temp = incoming_data.substring(start+1, stop);
                        //console.log(temp);
                        incoming_data = incoming_data.slice(stop+1);
                        var data = JSON.parse(temp);
                        if(data.cmd === "con"){
                            var board = new Array();
                            board["id"] = data.id;
    //                      board["language"] = data.language;
                            board["protocol"] = data.protocol;
                            board["name"] = data.name;
                            board["elements"] = new Array();
                            boards[data.id] = board;

                            console.log("\nNew board was connected to local PZP");
                            console.log("BOARD ID : " + board.id);
                    //      console.log("BOARD LANGUAGE : " + board.language);
                            console.log("BOARD PROTOCOL : " + board.protocol);
                            console.log("BOARD NAME : " + board.name);
                            send_con_ack();
                        }
                        else if(data.cmd === "ele"){
                            console.log("\nSerial driver - register new elements");
                            boards[data.id].elements = data.elements;
                            for(var i=0; i<data.elements.length ;i++){   
                                var tmp_ele = data.elements[i];
                                if(!isAlreadyRegistered(tmp_ele.id)){
                                    var str_type = (tmp_ele.element.sa == 0) ? "sensor" : "actuator";
                                    tmp_ele.element.description = "A webinos " + tmp_ele.element.type + " "  + str_type + " on " + boards[data.id].name + " [" + data.id + "]";
                                    try{
                                        tmp_ele.element.range = [tmp_ele.element.range.split("-")];
                                    }catch(e){}
                                    var id = registerFunc(driverId, tmp_ele.element.sa, tmp_ele.element);
                                    tmp_ele["serialport"] = serial;
                                    elementsList[id] = tmp_ele;
                                }
                                else
                                    console.log("Element is already registered");
                            }
                            send_ele_ack();
                        }
                        else if(data.cmd === "str"){
                            console.log("arduino sent ack for start");
                            console.log(JSON.stringify(data));
                        }
                        else if(data.cmd === "stp"){
                            console.log("arduino sent ack for stop");
                            console.log(JSON.stringify(data));
                        }
                        else if(data.cmd === "cfg"){
                            console.log("arduino sent ack for configure : " + JSON.stringify(data));
                            handlers[data.id].succCB(handlers[data.id].eId);
                        }
                        else if(data.cmd === "set"){
                            console.log("arduino sent ack for set");
                            console.log(JSON.stringify(data));
                        }
                        else if(data.cmd === "val"){
                            try{            
                                for(var i in elementsList){
                                    if(elementsList[i].id === data.id){
                                        var date = new Date();
                                        var now = date.getMinutes() + ":" + date.getSeconds();
                                        console.log("Received sensor value from arduino - " + JSON.stringify(data));

                                        if(sb){
                                            try{
                                                //console.log("convfunc : " + elementsList[i].element.convfunc);
                                                var func = elementsList[i].element.convfunc;
                                                var open = func.indexOf("(");
                                                var close = func.indexOf(")",close);
                                                var variable = func.substring(open+1,close).replace(/ /g, '');

                                                var first_equal_pos = func.indexOf("=");
                                                var code = func.substr(first_equal_pos+1);
                                                //console.log(code);
                                                var replaced_code = code.replace(/x/g, data.dat);

                                                //replaced_code = replaced_code.replace(/pow/g, "Math.pow");
                                                //console.log(replaced_code);
                                                
                                                sb.run( replaced_code, function( output ) {
                                                    console.log("Before conversion read data = "+data.dat);
                                                    console.log("After conversion read data = "+output.result);
                                                    if(output.result && output.result != "null"){
                                                        callbackFunc('data', i, output.result);
                                                    }
                                                    else
                                                        callbackFunc('data', i, data.dat);
                                                });
                                            }
                                            catch(e_conv){
                                                console.log(e_conv);
                                            }
                                        }
                                        else                                    
                                            callbackFunc('data', i, data.dat);

                                        break;
                                    }
                                }
                            }
                            catch(err){
                                console.log(err);
                                //console.log("Received values from non configured board. Please restart the board");
                            } 
                        }
                        incoming_data = "";
                    }
                }
                catch(e){
                    console.log(e);
                }    
            });
        }

        function send_con_ack(){
    //        console.log("pzp sent ack for incoming connection");
            if(serial)
                serial.write("cmd=con&eid=000&dat=ack\n");
        }

        function send_ele_ack(){
    //        console.log("pzp sent ack for incoming elements");
            if(serial)
                serial.write("cmd=ele&eid=000&dat=ack\n");
        }
    };

    function send_command(output_port, cmd, eid, dat){
        if(output_port)
            output_port.write("cmd="+cmd+"&eid="+eid+"&dat="+dat+"\n");
    }

    function init_serial(){
        try{
            var filePath = path.resolve(__dirname, "../../config.json");
            fs.readFile(filePath, function(err,data) {
                if (!err) {
                    var settings = JSON.parse(data.toString());
                    var drivers = settings.params.drivers;
                    for(var i in drivers){
                        if(drivers[i].type == "serial"){
                            var interfaces = drivers[i].interfaces;
                            for(var j in interfaces){
                                if(interfaces[j].port)
                                    var f = new SERIAL_PORT(interfaces[j].name, interfaces[j].port, (interfaces[j].rate | 9600));
                            }
                        }
                    }
                }
            });
        }
        catch(err){
            console.log("err : " + err);
        }    
    }


    
    /*
    * This function is called to initialize the driver.
    * @param dId Identifier of the driver
    * @param regFunc This is the function to call to register a new sensor/actuator
    * @param cbkFunc This is the function to call to send back data
    *
    */
    exports.init = function(dId, regFunc, remFunc, cbkFunc) {
        console.log('Serial driver init - id is ' + dId);
        driverId = dId;
        registerFunc = regFunc;
        removeFunction = remFunc;
        callbackFunc = cbkFunc;
        init_serial();
    };


    /*
    * This function is called to execute a command
    * @param cmd The command
    * @param eId Identifier of the element that should execute the command
    * @param data Data of the command
    *
    */
    exports.execute = function(cmd, eId, data, errorCB, successCB) {
        var native_element_id = elementsList[eId].id.split("_")[1];  //eg nativeid = "000001_001"
        var board_id = elementsList[eId].id.split("_")[0];
        var output_port = elementsList[eId].serialport;
        switch(cmd) {
            case 'cfg':
                //In this case cfg data are transmitted to the sensor/actuator
                //this data is in json(???) format
                console.log('Received cfg for element '+eId+', cfg is '+JSON.stringify(data));
                handlers[elementsList[eId].id] = {"succCB" : successCB, "errCB" : errorCB, "eId" : eId};
                var eventmode = (data.eventFireMode === "valuechange") ? VALUECHANGE_MODE:FIXEDINTERVAL_MODE;
                var param_data = data.timeout+":"+data.rate+":"+eventmode;
                send_command(output_port, CONFIGURE_CMD,native_element_id,param_data);
                break;
            case 'start':                                 
                //In this case the sensor should start data acquisition
                //the parameter data has value 'fixed' (in case of fixed interval
                // acquisition) or 'change' (in case od acquisition on value change)
                console.log('Received start command from API. Element : '+eId+', mode : '+data);
                send_command(output_port, START_LISTENING_CMD,native_element_id,NO_VALUE);
                break;
            case 'stop':
                //In this case the sensor should stop data acquisition
                //the parameter data can be ignored
                console.log('Received stop command from API. Element : '+eId);
                send_command(output_port, STOP_LISTENING_CMD,native_element_id,NO_VALUE);
                break;
            case 'value':
                //In this case the actuator should store the value
                //the parameter data is the value to store                
                console.log('Sent value for actuator '+eId+'; value is '+data);
                send_command(output_port, SET_ACTUATOR_VALUE_CMD,native_element_id,data);
                break;
            default:
                console.log('Serial driver - unrecognized cmd');
        }
     }
}());
