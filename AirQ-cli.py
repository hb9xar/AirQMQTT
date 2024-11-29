#!/usr/bin/python3
import sys
import requests
import json

# Example:
# $ ./AirQ-cli.py 192.168.131.112 '{"config":{"rtc":{"sleep_interval": 60}}}'
# pass config JSON via STDIN:
# $ echo '{"config":{"rtc":{"sleep_interval": 60}}}' | ./AirQ-cli.py 192.168.131.112 -
# $ cat config.json | ./AirQ-cli.py 192.168.131.112 -


if (len(sys.argv) <= 1):
    print(sys.argv[0] + " <AirQ-IP> [config-JSON | -]")
    exit(1)

if (len(sys.argv) > 1):
    airq_host=sys.argv[1]

airq_cfgdata=""
post=0
if (len(sys.argv) > 2):
    post=1
    airq_cfgdata=sys.argv[2]

    # if '-', read from STDIN
    if airq_cfgdata == '-':
        airq_cfgdata = sys.stdin.read()

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

print ("API call status: {0}".format(response.status_code))

if(response.ok):
    jData = json.loads(response.content)

    print("Response:".format(len(jData)))
    print(json.dumps(jData, indent=2))
else:
  # If response code is not ok (200), print the resulting http error code with description
    response.raise_for_status()
