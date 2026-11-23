#pragma once

class CoopWorkerCommandLineParser;
class StreamingPreferences;

int runCouchCoopWorker(const CoopWorkerCommandLineParser& parser,
                       StreamingPreferences* preferences);

