import firebase_admin
from firebase_admin import db
import json
import asyncio
import concurrent.futures

from DataStoreInterface import DataStoreInterface

## Firebase Real Time database storage
#
# REQUIRED: pip install firebase_admin
#

executor = concurrent.futures.ThreadPoolExecutor(max_workers=20)

class DataStore_FirebaseRT(DataStoreInterface):

    @classmethod
    async def create(cls, path_to_creds_file: str, databaseURL: str, root_path: str):
        self = DataStore_FirebaseRT()
        await self.init_storage(path_to_creds_file, databaseURL, root_path)
        return self

    # trivial constructor. use create() instead
    def __init__(self):
        pass

    def _init_storage_impl(self, path_to_creds_file: str, databaseURL: str, root_path: str):
        # Initialize connection to data storage.
        # Raises exception if not valid creds or app_path

        self.cred_obj = firebase_admin.credentials.Certificate(path_to_creds_file)
        self.default_app = firebase_admin.initialize_app(self.cred_obj, {
            'databaseURL':databaseURL
            })
        self.db_ref = db.reference(root_path)
        print("Firebase intialized. url: ", databaseURL, " db_ref: ", root_path)
        pass

    async def init_storage(self, path_to_creds_file: str, databaseURL: str, root_path: str):
        # Initialize connection to data storage.
        # Raises exception if not valid creds or app_path
        return await asyncio.get_event_loop().run_in_executor(executor, self._init_storage_impl, path_to_creds_file, databaseURL, root_path)

#    def push_many(self, many_data: list) -> dict:
#        for d in many_data:
#            self.push_data(d)

    async def push_data(self, data_to_push: dict) -> dict:
        # Push data object to storage as new object with json content
        await asyncio.get_event_loop().run_in_executor(executor, self.db_ref.push, data_to_push)
        #print("Pushed to firebase: ", data_to_push)

    def close(self):
        self.db_ref = None
        self.cred_obj = None
        firebase_admin.delete_app(self.default_app)
        self.default_app = None

        print("Firebase closed.")
        pass
