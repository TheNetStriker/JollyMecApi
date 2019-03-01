import sys
import requests
import json
import os.path
import pickle
import time
import logging

logging.basicConfig(filename='/tmp/jollymec.log', format='%(asctime)s %(message)s', level=logging.ERROR)

cookieFile = "/tmp/jollymec_cookies.bin"

loginurl = 'http://jollymec.efesto.web2app.it/de/login/'
ajaxurl = 'http://jollymec.efesto.web2app.it/de/ajax/action/frontend/response/ajax/'
username = "test@test.com"
password = "MyPassword"
heaterId = "MyHeaterId"
retrycount  = 1
retrycounter = 0

commandHeaders = {
    'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8',
    'X-Requested-With': 'XMLHttpRequest',
    'Accept': 'application/json, text/javascript, */*; q=0.01',
    'Referer': 'http://jollymec.efesto.web2app.it/de/heaters/action/manage/heater/' + heaterId + '/',
    'Origin': 'http://jollymec.efesto.web2app.it'}

session = requests.Session()

def save_cookies(requests_cookiejar, filename):
    with open(filename, 'wb') as f:
        pickle.dump(requests_cookiejar, f)

def load_cookies(filename):
    with open(filename, 'rb') as f:
        return pickle.load(f)

def login( username, password ):
    payload = {
        'login[username]': username, 
        'login[password]': password}
    
    loginHeaders = {
        'Content-Type': 'application/x-www-form-urlencoded',
        'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8',
        'Referer': 'http://jollymec.efesto.web2app.it/de/login/'}

    response = postSession(loginurl, data=payload, headers=loginHeaders)

    if response.status_code == 200:
        save_cookies(session.cookies, cookieFile)
        logging.info("Login successfull, cookies saved.")
        return { 'state': "OK" }
    else:
        logging.error("Login failed, status code: %s", response.status_code)
        return { 'state': "LOGIN STATUS CODE " + str(response.status_code) }

def set_power( power ):
    payload = {
        'method': 'write-parameters-queue', 
        'params': 'set-power=' + power,
        'device': heaterId}
    
    response = postSession(ajaxurl, data=payload, headers=commandHeaders)

    if response.status_code == 200:
        try:
            responseData = json.loads(response.text)

            if responseData["status"] == 0:
                return { 'state': "OK" }
            elif responseData["status"] == 1:
                return { 'state': "NOT LOGGED IN" }
            else:
                return { 'state': "SET POWER STATUS NOT OK:" + response.text }
        except ValueError:
            return handleValueError('set_power', response)
    else:
        return { 'state': "SET POWER STATUS CODE " + str(response.status_code) }

def get_state( ):
    payload = {
        'method': 'get-state', 
        'params': '1',
        'device': heaterId}
    
    response = postSession(ajaxurl, payload, commandHeaders)

    if response.status_code == 200:
        try:
            responseData = json.loads(response.text)
            
            if responseData["status"] == 0:
                return {
                    'state': "OK", 
                    'data': json.dumps(responseData["message"])
                }
            elif responseData["status"] == 1:
                return { 'state': "NOT LOGGED IN" }
            else:
                return { 'state': "GET STATE STATUS NOT OK:" + response.text }
        except ValueError:
            return handleValueError('get_state', response)
    else:
        return {'state': "GET STATE STATUS CODE " + str(response.status_code) }

def set_heater_on_off( on_off ):
    payload = {
        'method': 'heater-' + on_off, 
        'params': '1',
        'device': heaterId}
    
    response = postSession(ajaxurl, data=payload, headers=commandHeaders)

    if response.status_code == 200:
        try:
            responseData = json.loads(response.text)

            if responseData["status"] == 0:
                return { 'state': "OK" }
            elif responseData["status"] == 1:
                return { 'state': "NOT LOGGED IN" }
            else:
                return { 'state': "SET HEATER ON/OFF STATUS NOT OK:" + response.text }
        except ValueError:
            return handleValueError('set_heater_on_off', response)
    else:
        return ("SET HEATER ON/OFF STATUS CODE " + str(response.status_code))

def handleValueError( moduleName, response ):
    errorText = "Error parsing json in {}, response: {}".format(moduleName, response.text)
    logging.error(errorText)
    return { 'state': errorText }

def executeCommand( command, argument ):
    if command == "set_power":
        #Try to set power
        return set_power(power=argument)
    elif command == "set_heater_on_off":
        #Try to set on/off
        return set_heater_on_off(on_off=argument)
    elif command == "get_state":
        #Try to get state
        return get_state()
    else:
        return "NOT SUPPORTED COMMAND"

def handleResult( result ):
    if arg1 == "get_state":
        if (result["state"]) == "OK":
            print (result["data"])
        else:
            print (result["state"])
    else:
        print (result["state"])

def postSession( url, data, headers ):
    global retrycounter
    response = session.post(url=url, data=data, headers=headers)

    if response.text in "<title>Kommunikationsprobleme</title>" and retrycounter < retrycount:
        retrycounter = retrycounter + 1
        logging.warn("Communications error, trying again (retry %s of %s)", retrycounter, retrycount)
        time.sleep(5)
        return postSession( url, data, headers )
    else:
        return response

arg1 = str(sys.argv[1])

if arg1 == "get_state":
    arg2 = ""
else:
    arg2 = str(sys.argv[2])

logging.debug("Script called with the following arguments: %s %s", arg1, arg2)

#Check if cookie file exists
if os.path.isfile(cookieFile):
    session.cookies = load_cookies(cookieFile)
else:
    #Login if file does not exists
    result = login(username, password)
    if not result == "OK":
        print (result)
        quit()

result = executeCommand(arg1, arg2)

if result["state"] == "OK":
    handleResult(result)
elif result["state"] == "NOT LOGGED IN":
    #Login expired, trying one time to log in again
    result = login(username, password)

    if not result["state"] == "OK":
        print (result)
        quit()

    #Login successfull, trying again to set power
    result = executeCommand(arg1, arg2)
    handleResult(result)
else:
    print (result)
