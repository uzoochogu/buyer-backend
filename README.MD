# Buyer Backend

Internal development guide for the buyer backend service.

## Quick Start

### Directory Paths
- Frontend: `~/Documents/GitHub/dev/agent-app/buyer-frontend`
- Backend: `~/Documents/GitHub/dev/agent-app/buyer-backend`

Please note working directory and relative paths differs based on build system e.g. MSVC uses Debug/Release folders

## Frontend Development
### Installing Dependencies
 - Node - You can use [nvm](https://github.com/nvm-sh/nvm) to manage node versions. 23.9.0 was used for initial development (latest at time of writing).
 
using nvm
```bash
nvm install latest
nvm use 23.9.0
npm install
```

Running frontend
```bash
npm run dev
```

Navigate to localhost:5173 to view the frontend

## Backend Development

### Installing Dependencies
Install Postgres on your test machine, either [locally](https://www.postgresql.org/download/) or through [docker](https://hub.docker.com/_/postgres). 

vcpkg can be used to manage library dependencies.

Below are some of the dependencies that are used in the project.
- [drogon](https://github.com/drogonframework/drogon) - with some submodules and sub-dependencies.
- [argon2](https://github.com/P-H-C/phc-winner-argon2.git)
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp)

Installing using vcpkg
```bash
# triplet_name here depends on your system e.g. x64-windows-static or x64-windows
./vcpkg install drogon[core,ctl,orm,postgres]:triplet_name --recurse
./vcpkg install jwt-cpp:triplet_name
./vcpkg install argon2[hwopt,tool]:triplet_name
```

More information on setting up the drogon-ctl tool can be found [here](https://github.com/drogonframework/drogon/wiki/ENG-02-Installation#Drogon-Installation)

### Compilation
```bash
# Configure CMake
cmake -B ./build -S . "-DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build --config Release --parallel

# Build with clean
cmake --build build --config Release --parallel --clean-first
```

### Running the Backend
Modify the configuration files to setup correct backend runtime environment.
Create `test_config.json` and `config.json` from the provided sample files: [`config-sample.json`](./config-sample.json) and [`test_config-sample`](./test_config-sample.json).

Fill in the correct details especially the database connection details.

```bash
$ ./buyer-backend.exe --help
Usage: buyer-backend [OPTIONS]

Options:
  --test, -t       Run in test mode using test_config.json.
                   Searches up to 3 parent directories up.
  --config <file>  Use specified config file.
                   Searches up to 3 parent directories up.
  --help, -h       Display this help message and exit.
```

```bash
# Run with default config
./buyer-backend
```
By default it runs on 5555, but can be configured using the configuration file.

### Drogon Framework Commands

#### Create Components
```bash
# Create Filter
drogon_ctl create filter AuthMiddleware

# Create Controller
drogon_ctl create controller -h api::v1::login

# Create Model - Note that we currently don't use the drogon orm
drogon_ctl create model users
```

## Testing Backend
Integration tests using the Drogon built-in test framework.

### Configure Tests
This can be done once, or whenever the test configuration changes.

Create `setup_test_db.bat` and `setup_test_db.sh` from the provided sample files:  [`setup_test_db-sample.bat`](./scripts/setup_test_db-sample.bat) and [`setup_test_db-sample.sh`](./scripts/setup_test_db-sample.sh).

Fill in the correct database connection details.

```bash
cmake --build build --target configure_tests
```

### Build Tests
```bash
# Release build
cmake --build build --config Release --target buyer_backend_test

# Debug build
cmake --build build --config Debug --target buyer_backend_test
```

### Run Tests
```bash
# Run main application
./buyer-backend --test-mode

# Then run test
# From ~/buyer-backend/build/test
ctest -C Release
# or
ctest -C Debug
# Note that -V flag gives verbose output



# or navigate to the test executable and run it directly
./buyer_backend_test
```

### Notes for Integration testing:
1. Ensure that there no active connection to the PostgreSQL DB before configuring tests. This can cause errors in the setup scripts
2. Run main application in test mode in separate terminal before running integration tests or else integration tests segfaults.
   ```bash
   ./buyer-backend --test-mode 
   # or
   ./buyer-backend -t
   ```
   The test executable makes requests processed by the main application server. Running main server in test mode ensures that it is connected to the correct test database.

## Database Management

### Creating Database
```bash
createdb agentbackend
# or with custom parameters
createdb -h %DB_HOST% -p %DB_PORT% -U %DB_USER% %DB_NAME%
```

### Reset Database
```bash
psql -U postgres -d agentbackend -c "DROP SCHEMA public CASCADE; CREATE SCHEMA public;"
```

### Running Migrations
```bash
psql -U postgres -d agentbackend -f migrations/001_complete_schema.sql

# test DB ( or auto-generate using the configure_tests cmake target)
psqll -U postgres -d buyer_app_test -f migrations/001_complete_schema.sql
```

### Seeding Data
```bash
psql -U postgres -d agentbackend -f seeds/001_complete_seed_data.sql

# for test DB ( or auto-seed using the configure_tests cmake target)
psqll -U postgres -d buyer_app_test -f seeds/001_complete_seed_data.sql
```

## Debugging tips
* If you are getting a 500 error instead of a 400 error, then you are not validating the data coming from the request (body and Headers).
* If you are getting 200 or 201 error when you aren't supposed to, then you are not validating your input data. Maybe empty data?
* Note where you put logs, putting logs in the test side outputs in the test run, logs in the normal application outputs in the server logs
* Try to initialize the Json values as much as possible. Especially Array responses.
* Using the `["key"]` accessor in JsonCpp default constructs that field if it doesn't exists, prefer `.isMember("key")` to check for membership.
* If you want to inspect the Database after a test run, remove the teardown delete queries, recompile and rerun tests. This way you have access to it after a run for more better investigations.
* Use Drogon LOG_INFO and LOG_ERROR macros for logging. For Json responses this helps: 
```cpp
  LOG_INFO << json_response.toStyledString();
  // or use .type() to check e.g.
  if (json_response.type() == Json::ValueType::objectValue)
```
