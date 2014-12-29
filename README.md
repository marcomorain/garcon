# garçon

Garçon is a local HTTP fileserver. It is designed to make it simple to serve the files in a local directory over HTTP. Garçon is intended to be used for local development, and should is not intended to replace the nginx or apache webservers.

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

## License 

Garçon is released under the [MIT License](http://www.opensource.org/licenses/MIT).
