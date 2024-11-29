#!/usr/bin/python3
import sys
import requests
import json

# Example:
# $ ./AirQ-cli.py 192.168.131.112 '{"config":{"rtc":{"sleep_interval": 60}}}'


if (len(sys.argv) <= 1):
    print(sys.argv[0] + " <AirQ-IP> [config-JSON]")
    exit(1)

if (len(sys.argv) > 1):
    airq_host=sys.argv[1]

airq_cfgdata=""
post=0
if (len(sys.argv) > 2):
    post=1
    airq_cfgdata=sys.argv[2]
    try:
        json.loads(airq_cfgdata)
    except ValueError as e:
      print("invalid JSON data.")
      exit(1)


url = 'http://'+airq_host+'/api/v1/config'

if (post):
    response = requests.post(url, data=airq_cfgdata)
else:
    response = requests.get(url)

print (response.status_code)

if(response.ok):

    # Loading the response data into a dict variable
    # json.loads takes in only binary or string variables so using content to fetch binary content
    # Loads (Load String) takes a Json file and converts into python data structure (dict or list, depending on JSON)
    jData = json.loads(response.content)

    print("The response contains {0} properties".format(len(jData)))
    print("\n")
    print(json.dumps(jData, indent=2))
else:
  # If response code is not ok (200), print the resulting http error code with description
    response.raise_for_status()
