# informal async interface class for datastorage

from array import array


class DataStoreInterface:
    async def init_storage(self, cpath_to_creds_file: str, databaseURL: str):
        # Initialize connection to data storage.
        pass

    async def push_data(self, data_to_push: dict):
        # Push data object to storage
        pass

    async def push_many(self, many_data: list):
        # Push data object to storage
        pass

    async def close():
        # Close connection to data storage.
        pass