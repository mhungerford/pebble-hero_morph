#!/usr/bin/python
import sys
import json
import os.path
from os import listdir
from os.path import isfile, join

def update_appinfo(filename, resource_dir):
    # load old dictionary if possible
    if os.path.lexists(filename):
        appinfo_json = json.load(open(filename, "rb"))

    files = [ f for f in listdir(resource_dir) if isfile(join(resource_dir,f)) ]
    files.sort()

    #media_entries = appinfo_json['resources']['media']
    media_entries = []

    #add our fonts manually
    media_entries.append({
        "type":"font","name":"FONT_BOXY_TEXT_30",
        "characterRegex":"[:0-9]",
        "file":"Boxy_Text.ttf"})

    media_entries.append({
        "type":"font","name":"FONT_BOXY_TEXT_18",
        "characterRegex":"[ /0-9MTWFSadehinortu]",
        "file":"Boxy_Text.ttf"})

    i = 0
    for file in files:
        if '.png' in file:
            media_entries.append({"type":"raw","name":"IMAGE_" + str(i),"file":file})
            i = i + 1
    
    appinfo_json['resources']['media'] = media_entries

    # write the json dictionary
    json.dump(appinfo_json, open(filename, "wb"), indent=2, sort_keys=False)
    return True


def main():

    # arguments, print an example of correct usage.
    if len(sys.argv) != 3:
        print("********************")
        print("Usage suggestion:")
        print("python " + sys.argv[0] + " <appinfo.json> <resources>")
        print("********************")
        exit()

    filename = sys.argv[1]
    resource_dir = sys.argv[2]

    update_appinfo(filename, resource_dir);


if __name__ == '__main__':
    main()

