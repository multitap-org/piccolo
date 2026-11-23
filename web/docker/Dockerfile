# piccolo-web: the browser build served with the headers its threads need.
#   web/package.sh                      -> web/build/dist/piccolo-<ver>-web/
#   docker build -t piccolo-web -f web/docker/Dockerfile --build-context config=web/docker \
#       web/build/dist/piccolo-<ver>-web
#   docker run --rm -p 8080:80 piccolo-web       -> http://localhost:8080/
# The build context is the unzipped folder: the same files as the zip.
FROM nginx:1.27-alpine
COPY --from=config nginx.conf /etc/nginx/conf.d/default.conf
COPY . /usr/share/nginx/html/
# get-piccolo.app serves this image under /play/ (a proxy location that keeps
# the path): play -> . makes /play/<page> the same file as /<page>.
RUN ln -s . /usr/share/nginx/html/play
