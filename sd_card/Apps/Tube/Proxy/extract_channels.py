#!/usr/bin/env python3
import json
import sys

def main():
    db_path = 'ltm-database.json'
    conf_path = 'proxy.conf'
    
    try:
        with open(db_path, 'r', encoding='utf-8') as f:
            db = json.load(f)
    except FileNotFoundError:
        print(f"Error: {db_path} not found.")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Error: {db_path} is not a valid JSON file.")
        sys.exit(1)
        
    channels = db.get('data', {}).get('subscribedChannels', [])
    if not channels:
        print("No channels found in ltm-database.json")
        sys.exit(1)
        
    with open(conf_path, 'w', encoding='utf-8') as f:
        for ch in channels:
            cid = ch.get('id')
            cname = ch.get('name', 'Unknown Channel')
            if cid:
                f.write(f"{cid},{cname}\n")
                
    print(f"Successfully extracted {len(channels)} channels to {conf_path}.")

if __name__ == '__main__':
    main()
