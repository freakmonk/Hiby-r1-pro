import feed_manager
import time

fm = feed_manager.FeedManager()
time.sleep(10)
print(f"List length: {len(fm.get_list())}")
print(fm.get_list())
