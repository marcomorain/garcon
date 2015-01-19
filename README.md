# garçon

[![Circle CI](https://circleci.com/gh/marcomorain/garcon.svg?style=svg)](https://circleci.com/gh/marcomorain/garcon)

Garçon is a local HTTP fileserver. It is designed to make it simple to serve the files in a local directory over HTTP. Garçon is intended to be used for local development, and should is not intended to replace the nginx or apache webservers.

I use garcon in place of having to configure `nginx` to serve a local folder when doing web development. Garcon does the same job as running `python -m SimpleHTTPServer 8888`

HTTP GET is the only method that is implemented. A request with any method other than GET will receive a response code of 405. Garcon will serve any file that the process has access to from the specified directory, or any sub-directories. Requests are served one at a time from a single thread.

## Usage

Using garcon is simple – run `garcon` in a directory to make the contents available over HTTP on `localhost:8888`. You can specify the port and directory using the `--port` and `--directory` command-line arguments.

```
  Usage: garcon [options]

  Options:

    -V, --version                 output program version
    -h, --help                    output help information
    -d, --directory [arg]         The root directory to serve files from (default to the current working directory)
    -p, --port [arg]              Which port to listen on (default 8888)
```

## CORS
Garçon adds the necessary headers to allow [CORS](http://en.wikipedia.org/wiki/Cross-origin_resource_sharing) requests to be accepted. These are `Access-Control-Allow-Methods: GET` `Access-Control-Allow-Origin: *`.

## Logging
Requests are logged to `stdout` in Apache Combined Log Format, which looks like this:

```
Garçon! Serving content from /Users/marc/dev/garcon on port 8888
127.0.0.1 - - [2014-12-30T00:02:51+0000] GET /index.html 404 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/39.0.2171.95 Safari/537.36"
127.0.0.1 - - [2014-12-30T00:03:06+0000] GET /garcon.h 404 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/39.0.2171.95 Safari/537.36"
127.0.0.1 - - [2014-12-30T00:03:14+0000] GET /favicon.ico 404 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) AppleWebKit/600.2.5 (KHTML, like Gecko) Version/8.0.2 Safari/600.2.5"
127.0.0.1 - - [2014-12-30T00:03:14+0000] GET /apple-touch-icon-precomposed.png 404 "(null)"
127.0.0.1 - - [2014-12-30T00:03:14+0000] GET /apple-touch-icon.png 404 "(null)"
127.0.0.1 - - [2014-12-30T00:03:19+0000] GET /garcon.h 404 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:34.0) Gecko/20100101 Firefox/34.0"
127.0.0.1 - - [2014-12-30T00:03:20+0000] GET /favicon.ico 404 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:34.0) Gecko/20100101 Firefox/34.0"
127.0.0.1 - - [2014-12-30T00:03:27+0000] GET /Makefile 200 "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.10; rv:34.0) Gecko/20100101 Firefox/34.0"
```
## License

Garçon is released under the [MIT License](LICENSE).
