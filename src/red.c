#include <stdio.h>
#include <curl/curl.h>
 
int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
 
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://example.com/");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
 
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "Error: %s\n", curl_easy_strerror(res));
 
        curl_easy_cleanup(curl);
    }
 
    curl_global_cleanup();
    return 0;
}