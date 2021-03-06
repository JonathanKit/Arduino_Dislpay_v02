/*
ArduinoSpotify - An Arduino library to wrap the Spotify API

Copyright (c) 2020  Brian Lough.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "ArduinoSpotify.h"
#include "iostream"

ArduinoSpotify::ArduinoSpotify(Client &client)
{
    this->client = &client;
}

ArduinoSpotify::ArduinoSpotify(Client &client, char *bearerToken)
{
    this->client = &client;
    sprintf(this->_bearerToken, "Bearer %s", bearerToken);
    initStructs();
}

ArduinoSpotify::ArduinoSpotify(Client &client, const char *clientId, const char *clientSecret, const char *refreshToken)
{
    this->client = &client;
    this->_clientId = clientId;
    this->_clientSecret = clientSecret;
    this->_refreshToken = refreshToken;
    initStructs();
}

int ArduinoSpotify::makeRequestWithBody(const char *type, const char *command, const char *authorization, const char *body, const char *contentType, const char *host)
{
    client->flush();
    #ifdef SPOTIFY_DEBUG
        Serial.println(host);
    #endif
    client->setTimeout(SPOTIFY_TIMEOUT);
    if (!client->connect(host, portNumber))
    {
        Serial.println(F("Connection failed"));
        return -1;
    }

    // give the esp a breather
    yield();

    // Send HTTP request
    client->print(type);
    client->print(command);
    client->println(F(" HTTP/1.1"));

    //Headers
    client->print(F("Host: "));
    client->println(host);

    client->println(F("Accept: application/json"));
    client->print(F("Content-Type: "));
    client->println(contentType);

    if (authorization != NULL)
    {
        client->print(F("Authorization: "));
        client->println(authorization);
    }

    client->println(F("Cache-Control: no-cache"));

    client->print(F("Content-Length: "));
    client->println(strlen(body));

    client->println();

    client->print(body);

    if (client->println() == 0)
    {
        Serial.println(F("Failed to send request"));
        return -2;
    }

    int statusCode = getHttpStatusCode();
    return statusCode;
}

int ArduinoSpotify::makePutRequest(const char *command, const char *authorization, const char *body, const char *contentType, const char *host)
{
    return makeRequestWithBody("PUT ", command, authorization, body, contentType);
}

int ArduinoSpotify::makePostRequest(const char *command, const char *authorization, const char *body, const char *contentType, const char *host)
{
    return makeRequestWithBody("POST ", command, authorization, body, contentType, host);
}

int ArduinoSpotify::makeGetRequest(const char *command, const char *authorization, const char *accept, const char *host)
{
    client->flush();
    client->setTimeout(SPOTIFY_TIMEOUT);
    if(!client->connected()) {
        if (!client->connect(host, portNumber)) {
            Serial.println(F("Connection failed"));
            return -1;
        }
    }
    
    // give the esp a breather
    yield();

    // Send HTTP request
    client->print(F("GET "));
    client->print(command);
    client->println(F(" HTTP/1.1"));

    //Headers
    client->print(F("Host: "));
    client->println(host);

    if (accept != NULL)
    {
        client->print(F("Accept: "));
        client->println(accept);
    }

    if (authorization != NULL)
    {
        client->print(F("Authorization: "));
        client->println(authorization);
    }

    client->println(F("Cache-Control: no-cache"));

    if (client->println() == 0)
    {
        Serial.println(F("Failed to send request"));
        return -2;
    }

    int statusCode = getHttpStatusCode();

    return statusCode;
}

void ArduinoSpotify::setRefreshToken(const char *refreshToken)
{
    _refreshToken = refreshToken;
}

bool ArduinoSpotify::refreshAccessToken()
{
    char body[300];
    sprintf(body, refreshAccessTokensBody, _refreshToken, _clientId, _clientSecret);

#ifdef SPOTIFY_DEBUG
    Serial.println(body);
    printStack();
#endif

    int statusCode = makePostRequest(SPOTIFY_TOKEN_ENDPOINT, NULL, body, "application/x-www-form-urlencoded", SPOTIFY_ACCOUNTS_HOST);
    if (statusCode > 0)
    {
        skipHeaders();
    }
    unsigned long now = millis();

#ifdef SPOTIFY_DEBUG
    Serial.print("status Code");
    Serial.println(statusCode);
#endif

    bool refreshed = false;
    if (statusCode == 200)
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, *client);
        if (!error)
        {
            sprintf(this->_bearerToken, "Bearer %s", doc["access_token"].as<char *>());
            int tokenTtl = doc["expires_in"];             // Usually 3600 (1 hour)
            tokenTimeToLiveMs = (tokenTtl * 1000) - 2000; // The 2000 is just to force the token expiry to check if its very close
            timeTokenRefreshed = now;
            refreshed = true;
        }
    }
    else
    {
        parseError();
    }

    closeClient();
    return refreshed;
}

bool ArduinoSpotify::checkAndRefreshAccessToken()
{
    unsigned long timeSinceLastRefresh = millis() - timeTokenRefreshed;
    if (timeSinceLastRefresh >= tokenTimeToLiveMs)
    {
        Serial.println("Refresh of the Access token is due, doing that now.");
        closeClient();
        bool success = refreshAccessToken();
        return success;
    }

    // Token is still valid
    return true;
}

const char *ArduinoSpotify::requestAccessTokens(const char *code, const char *redirectUrl)
{

    char body[500];
    sprintf(body, requestAccessTokensBody, code, redirectUrl, _clientId, _clientSecret);

#ifdef SPOTIFY_DEBUG
    Serial.println(body);
#endif

    int statusCode = makePostRequest(SPOTIFY_TOKEN_ENDPOINT, NULL, body, "application/x-www-form-urlencoded", SPOTIFY_ACCOUNTS_HOST);
    if (statusCode > 0)
    {
        skipHeaders();
    }
    unsigned long now = millis();

#ifdef SPOTIFY_DEBUG
    Serial.print("status Code");
    Serial.println(statusCode);
#endif

    if (statusCode == 200)
    {
        DynamicJsonDocument doc(1000);
        DeserializationError error = deserializeJson(doc, *client);
        if (!error)
        {
            sprintf(this->_bearerToken, "Bearer %s", doc["access_token"].as<char *>());
            _refreshToken = doc["refresh_token"].as<char *>();
            int tokenTtl = doc["expires_in"];             // Usually 3600 (1 hour)
            tokenTimeToLiveMs = (tokenTtl * 1000) - 2000; // The 2000 is just to force the token expiry to check if its very close
            timeTokenRefreshed = now;
        }
    }
    else
    {
        parseError();
    }

    closeClient();
    return _refreshToken;
}

bool ArduinoSpotify::play(const char *deviceId)
{
    char command[100] = SPOTIFY_PLAY_ENDPOINT;
    return playerControl(command, deviceId);
}

bool ArduinoSpotify::playAdvanced(char *body, const char *deviceId)
{
    char command[100] = SPOTIFY_PLAY_ENDPOINT;
    return playerControl(command, deviceId, body);
}

bool ArduinoSpotify::pause(const char *deviceId)
{
    char command[100] = SPOTIFY_PAUSE_ENDPOINT;
    return playerControl(command, deviceId);
}

bool ArduinoSpotify::setVolume(int volume, const char *deviceId)
{
    char command[125];
    sprintf(command, SPOTIFY_VOLUME_ENDPOINT, volume);
    return playerControl(command, deviceId);
}

bool ArduinoSpotify::playerControl(char *command, const char *deviceId, const char *body)
{
    if (deviceId[0] != 0)
    {
        char *questionMarkPointer;
        questionMarkPointer = strchr(command, '?');
        char deviceIdBuff[50];
        if (questionMarkPointer == NULL)
        {
            sprintf(deviceIdBuff, "?device_id=%s", deviceId);
        }
        else
        {
            // params already started
            sprintf(deviceIdBuff, "&device_id=%s", deviceId);
        }
        strcat(command, deviceIdBuff);
    }

#ifdef SPOTIFY_DEBUG
    Serial.println(command);
    Serial.println(body);
#endif

    if (autoTokenRefresh)
    {
        checkAndRefreshAccessToken();
    }
    int statusCode = makePutRequest(command, _bearerToken, body);

    closeClient();
    //Will return 204 if all went well.
    return statusCode == 204;
}

bool ArduinoSpotify::playerNavigate(char *command, const char *deviceId)
{
    if (deviceId[0] != 0)
    {
        char deviceIdBuff[50];
        sprintf(deviceIdBuff, "?device_id=%s", deviceId);
        strcat(command, deviceIdBuff);
    }

#ifdef SPOTIFY_DEBUG
    Serial.println(command);
#endif

    if (autoTokenRefresh)
    {
        checkAndRefreshAccessToken();
    }
    int statusCode = makePostRequest(command, _bearerToken);

    closeClient();
    //Will return 204 if all went well.
    return statusCode == 204;
}

bool ArduinoSpotify::nextTrack(const char *deviceId)
{
    char command[100] = SPOTIFY_NEXT_TRACK_ENDPOINT;
    return playerNavigate(command, deviceId);
}

bool ArduinoSpotify::previousTrack(const char *deviceId)
{
    char command[100] = SPOTIFY_PREVIOUS_TRACK_ENDPOINT;
    return playerNavigate(command, deviceId);
}
bool ArduinoSpotify::seek(int position, const char *deviceId)
{
    char command[100] = SPOTIFY_SEEK_ENDPOINT;
    char tempBuff[100];
    sprintf(tempBuff, "?position_ms=%d", position);
    strcat(command, tempBuff);
    if (deviceId[0] != 0)
    {
        sprintf(tempBuff, "?device_id=%s", deviceId);
        strcat(command, tempBuff);
    }

#ifdef SPOTIFY_DEBUG
    Serial.println(command);
    printStack();
#endif

    if (autoTokenRefresh)
    {
        checkAndRefreshAccessToken();
    }
    int statusCode = makePutRequest(command, _bearerToken);
    closeClient();
    //Will return 204 if all went well.
    return statusCode == 204;
}

bool ArduinoSpotify::transferPlayback(const char *deviceId, bool play)
{
    char body[100];
    sprintf(body, "{\"device_ids\":[\"%s\"],\"play\":\"%s\"}", deviceId, (play?"true":"false"));

#ifdef SPOTIFY_DEBUG
    Serial.println(SPOTIFY_PLAYER_ENDPOINT);
    Serial.println(body);
    printStack();
#endif

    if (autoTokenRefresh)
    {
        checkAndRefreshAccessToken();
    }
    int statusCode = makePutRequest(SPOTIFY_PLAYER_ENDPOINT, _bearerToken, body);
    closeClient();
    //Will return 204 if all went well.
    return statusCode == 204;
}

CurrentlyPlaying ArduinoSpotify::getCurrentlyPlaying(const char *market)
{
    char command[50] = SPOTIFY_CURRENTLY_PLAYING_ENDPOINT;
    if (market[0] != 0)
    {
        char marketBuff[15];
        sprintf(marketBuff, "?market=%s", market);
        strcat(command, marketBuff);
    }

#ifdef SPOTIFY_DEBUG
    Serial.println(command);
    printStack();
#endif

    // Get from https://arduinojson.org/v6/assistant/
    const size_t bufferSize = currentlyPlayingBufferSize;
    //CurrentlyPlaying currentlyPlaying;
    // This flag will get cleared if all goes well
    currentlyPlaying.error = true;
    if (autoTokenRefresh)
    {
        checkAndRefreshAccessToken();
    }
    int statusCode = makeGetRequest(command, _bearerToken);

Serial.print("Status Code: ");
    Serial.println(statusCode);
    if (statusCode < 0) {
        closeClient();
    }




#ifdef SPOTIFY_DEBUG
    Serial.print("Status Code: ");
    Serial.println(statusCode);
    printStack();
#endif
    if (statusCode > 0)
    {
        skipHeaders();
        currentlyPlaying.statusCode = statusCode;
    }

    if (statusCode == 200)
    {
        //Apply Json Filter: https://arduinojson.org/v6/example/filter/
        DynamicJsonDocument filter(288);
        //StaticJsonDocument<288> filter;
        filter["is_playing"] = true;
        filter["progress_ms"] = true;

        JsonObject filter_item = filter.createNestedObject("item");
        filter_item["duration_ms"] = true;
        filter_item["name"] = true;
        filter_item["uri"] = true;
        filter_item["id"] = true;
        filter_item["popularity"] = true;

        JsonObject filter_item_artists_0 = filter_item["artists"].createNestedObject();
        filter_item_artists_0["name"] = true;
        filter_item_artists_0["uri"] = true;

        JsonObject filter_item_album = filter_item.createNestedObject("album");
        filter_item_album["name"] = true;
        filter_item_album["uri"] = true;

        // Allocate DynamicJsonDocument
        DynamicJsonDocument doc(bufferSize);

        // Parse JSON object
        DeserializationError error = deserializeJson(doc, *client, DeserializationOption::Filter(filter));
        if (!error)
        {
#ifdef SPOTIFY_DEBUG
            serializeJsonPretty(doc, Serial);
#endif
            JsonObject item = doc["item"];
            JsonObject firstArtist = item["artists"][0];

            // ------------Artist--------
            strncpy(currentlyPlaying.firstArtistName, firstArtist["name"].as<const char *>(), SPOTIFY_NAME_CHAR_LENGTH);
            currentlyPlaying.firstArtistName[SPOTIFY_NAME_CHAR_LENGTH-1] = '\0'; //In case the song was longer than the size of buffer
            
            // create a shorted version to display
            strcpy(currentlyPlaying.shortFirstArtistName, currentlyPlaying.firstArtistName);
            auto index_p = strchr(currentlyPlaying.shortFirstArtistName, '&');
            if (index_p != NULL) {
                *index_p = '\0';
            }
            
            strncpy(currentlyPlaying.firstArtistUri, firstArtist["uri"].as<const char *>(), SPOTIFY_URI_CHAR_LENGTH);
            currentlyPlaying.firstArtistUri[SPOTIFY_URI_CHAR_LENGTH-1] = '\0';
            //currentlyPlaying.firstArtistName = (char *)firstArtist["name"].as<char *>();
            //currentlyPlaying.firstArtistUri = (char *)firstArtist["uri"].as<char *>();

            // ------------Album------------
            strncpy(currentlyPlaying.albumName, item["album"]["name"].as<const char *>(), SPOTIFY_NAME_CHAR_LENGTH);
            currentlyPlaying.albumName[SPOTIFY_NAME_CHAR_LENGTH-1] = '\0';
            strncpy(currentlyPlaying.albumUri, item["album"]["uri"].as<const char *>(), SPOTIFY_URI_CHAR_LENGTH);
            currentlyPlaying.albumUri[SPOTIFY_URI_CHAR_LENGTH-1] = '\0';
            //currentlyPlaying.albumName = (char *)item["album"]["name"].as<char *>();
            //currentlyPlaying.albumUri = (char *)item["album"]["uri"].as<char *>();

            // -----------Track-----------------
            strncpy(currentlyPlaying.trackId, item["id"].as<const char *>(), SPOTIFY_URI_CHAR_LENGTH);
            currentlyPlaying.trackId[SPOTIFY_URI_CHAR_LENGTH-1] = '\0';
            strncpy(currentlyPlaying.trackName, item["name"].as<const char *>(), SPOTIFY_NAME_CHAR_LENGTH);
            currentlyPlaying.trackName[SPOTIFY_NAME_CHAR_LENGTH-1] = '\0';

            // create a shorted version to display
            strcpy(currentlyPlaying.shortTrackName, currentlyPlaying.trackName);
            index_p = strchr(currentlyPlaying.shortTrackName, '(');
            if (index_p != NULL) {
                *index_p = '\0';
            }
            index_p = strchr(currentlyPlaying.shortTrackName, '[');
            if (index_p != NULL) {
                *index_p = '\0';
            }
            index_p = strchr(currentlyPlaying.shortTrackName, '-');
            if (index_p != NULL) {
                *index_p = '\0';
            }
            
            currentlyPlaying.trackPopularity = item["popularity"].as<short>();
            strncpy(currentlyPlaying.trackUri, item["uri"].as<char *>(), SPOTIFY_URI_CHAR_LENGTH);
            currentlyPlaying.trackUri[SPOTIFY_URI_CHAR_LENGTH-1] = '\0';
            //currentlyPlaying.trackName = (char *)item["name"].as<char *>();
            //currentlyPlaying.trackUri = (char *)item["uri"].as<char *>();

            // ------------------Rest information----------------------------------------------
            currentlyPlaying.isPlaying = doc["is_playing"].as<bool>();

            currentlyPlaying.progressMs = doc["progress_ms"].as<long>();
            currentlyPlaying.duraitonMs = item["duration_ms"].as<long>();

            currentlyPlaying.error = false;
        }
        else
        {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(error.c_str());
        }
    }
    if (statusCode == 204)
    {
        currentlyPlaying.error = false;
    }
    // (jh) not closing the client to save time on a webcall
    closeClient();
    return currentlyPlaying;
}

AudioFeatures ArduinoSpotify::getAudioFeatures(const char *market, const char *trackId) {
    char command[100] = SPOTIFY_AUDIO_FEATURES_ENDPOINT;
    if (trackId[0] != 0) {
        strncat(command, trackId, 100);
    }
        if (market[0] != 0) {
        char marketBuff[15];
        sprintf(marketBuff, "?market=%s", market);
        strncat(command, marketBuff, 100);
    }
    #ifdef SPOTIFY_DEBUG
        Serial.println(command);
        printStack();
    #endif

    const size_t bufferSize = audioFeaturesBufferSize;
    // This flag will get cleared if all goes well
    audioFeatures.error = true;
    if (autoTokenRefresh) {
        checkAndRefreshAccessToken();
    }
    int statusCode = makeGetRequest(command, _bearerToken);
    #ifdef SPOTIFY_DEBUG
        Serial.print("Status Code: ");
        Serial.println(statusCode);
        printStack();
    #endif

    if (statusCode > 0) {
        skipHeaders();
        audioFeatures.statusCode = statusCode;
    }
    if (statusCode == 200) {
        // Allocate DynamicJsonDocument
        DynamicJsonDocument doc(bufferSize);
        
        // Parse JSON object
        DeserializationError error = deserializeJson(doc, *client);
        if (!error) {

            audioFeatures.danceability = doc["danceability"].as<float>();
            audioFeatures.energy = doc["energy"].as<float>();
            audioFeatures.key = doc["key"].as<int>();
            audioFeatures.loudness = doc["loudness"].as<float>();
            audioFeatures.mode = doc["mode"].as<int>();
            audioFeatures.speechiness = doc["speechiness"].as<float>();
            audioFeatures.acousticness = doc["acousticness"].as<float>();
            audioFeatures.instrumentalness = doc["instrumentalness"].as<float>();
            audioFeatures.liveness = doc["liveness"].as<float>();
            audioFeatures.valence = doc["valence"].as<float>();
            audioFeatures.tempo = doc["tempo"].as<float>();

            audioFeatures.error = false;
        } else {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(error.c_str());
        }
    #ifdef SPOTIFY_DEBUG
                serializeJsonPretty(doc, Serial);
    #endif

    }
    // 204 means successful call but no content
    if (statusCode == 204) {
        // TODO(jh) add empty content to the variables to show that there is no song playing
        audioFeatures.error = false;
    }

    return audioFeatures;
}

int ArduinoSpotify::getContentLength()
{

    if (client->find("Content-Length:"))
    {
        int contentLength = client->parseInt();
#ifdef SPOTIFY_DEBUG
        Serial.print(F("Content-Length: "));
        Serial.println(contentLength);
#endif
        return contentLength;
    }

    return -1;
}

void ArduinoSpotify::skipHeaders(bool tossUnexpectedForJSON)
{
    // Skip HTTP headers
    if (!client->find("\r\n\r\n"))
    {
        Serial.println(F("Invalid response"));
        return;
    }

    if (tossUnexpectedForJSON)
    {
        // Was getting stray characters between the headers and the body
        // This should toss them away
        while (client->available() && client->peek() != '{')
        {
            char c = 0;
            client->readBytes(&c, 1);
#ifdef SPOTIFY_DEBUG
            Serial.print(F("Tossing an unexpected character: "));
            Serial.println(c);
#endif
        }
    }
}

int ArduinoSpotify::getHttpStatusCode()
{
    // Check HTTP status
    if (client->find("HTTP/1.1"))
    {
        int statusCode = client->parseInt();
#ifdef SPOTIFY_DEBUG
        Serial.print(F("Status Code: "));
        Serial.println(statusCode);
#endif
        return statusCode;
    }

    return -1;
}

void ArduinoSpotify::parseError()
{
    DynamicJsonDocument doc(1000);
    DeserializationError error = deserializeJson(doc, *client);
    if (!error)
    {
        Serial.print(F("getAuthToken error"));
        serializeJson(doc, Serial);
    }
    else
    {
        Serial.print(F("Could not parse error"));
    }
}

void ArduinoSpotify::lateInit(const char *clientId, const char *clientSecret, const char *refreshToken)
{
    this->_clientId = clientId;
    this->_clientSecret = clientSecret;
    this->_refreshToken = refreshToken;
    initStructs();
}

void ArduinoSpotify::initStructs()
{
    currentlyPlaying.firstArtistName = (char *)malloc(SPOTIFY_NAME_CHAR_LENGTH);
    currentlyPlaying.shortFirstArtistName = (char *)malloc(SPOTIFY_NAME_CHAR_LENGTH);
    currentlyPlaying.firstArtistUri = (char *)malloc(SPOTIFY_URI_CHAR_LENGTH);
    currentlyPlaying.albumName = (char *)malloc(SPOTIFY_NAME_CHAR_LENGTH);
    currentlyPlaying.albumUri = (char *)malloc(SPOTIFY_URI_CHAR_LENGTH);
    currentlyPlaying.trackId = (char *)malloc(SPOTIFY_URI_CHAR_LENGTH);
    currentlyPlaying.trackName = (char *)malloc(SPOTIFY_NAME_CHAR_LENGTH);
    currentlyPlaying.shortTrackName = (char *)malloc(SPOTIFY_NAME_CHAR_LENGTH);
    currentlyPlaying.trackUri = (char *)malloc(SPOTIFY_URI_CHAR_LENGTH);
}

// Not sure why this would ever be needed, but sure why not.
void ArduinoSpotify::destroyStructs()
{
    free(currentlyPlaying.firstArtistName);
    free(currentlyPlaying.shortFirstArtistName);
    free(currentlyPlaying.firstArtistUri);
    free(currentlyPlaying.albumName);
    free(currentlyPlaying.albumUri);
    free(currentlyPlaying.trackId);
    free(currentlyPlaying.trackName);
    free(currentlyPlaying.shortTrackName);
    free(currentlyPlaying.trackUri);

}

void ArduinoSpotify::closeClient()
{
    if (client->connected())
    {
#ifdef SPOTIFY_DEBUG
        Serial.println(F("Closing client"));
#endif
        Serial.println(F("Closing client ------------------------------------------"));
        client->stop();
    }
}

#ifdef SPOTIFY_DEBUG
void ArduinoSpotify::printStack()
{
    char stack;
    Serial.print (F("stack size "));
    Serial.println (stack_start - &stack);
}
#endif
