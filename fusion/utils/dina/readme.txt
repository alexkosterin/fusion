Dina is a swiss knife of Fusion tag manipulation.

From help:
  [--host=HOST] [--port=PORT] --profile=PROFILE MODE
  MODE: one of create, delete, link, read, or write
    create: create new tag, or persistent tag with initial value
      syntax: --create=TAG-NAME|--create-persist=tag-name
      if creating persistent tag, then you must provide inital value, using either: --string=..., --int=..., --double=..., or --bool=1|0

    delete: delete existing tag
      syntax: --delete=TAG-NAME

    link: create a link to existing tag
      syntax: --link=NEW-TAG --target=EXISTING-TAG

    read: display/spy on values published for given tag
      sytax: (--bool|--int|--double|--string) --read --mgs=TAG-NAME
      optionally you may want add:
      --show-src - to show message source client id
      --show-org - to show message origination time stamp
      --show-seq - to show message sequence number
      press ^C to exit

    write: publish value(s) for given tag
      syntax:(--bool|--int|--double|--string) --write --mgs=TAG-NAME
      then follow prompt, enter empty value or press ^C to exit


   TAG-NAME can be prefixed with profile name: PROFILE:NAME
   if profile portion is missing, then default profile is assumed (the one provided by --profile=... argument)

   default host 127.0.0.1
   default port 3001