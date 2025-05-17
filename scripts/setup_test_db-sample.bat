@echo off
REM Database configuration
set DB_NAME=buyer_app_test
set DB_USER=postgres
set DB_PASSWORD=yourpassword
set DB_HOST=127.0.0.1
set DB_PORT=5432

REM Set password for PostgreSQL commands
set PGPASSWORD=%DB_PASSWORD%

echo Dropping database if it exists...
dropdb --if-exists -h %DB_HOST% -p %DB_PORT% -U %DB_USER% %DB_NAME%
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to drop database
    exit /b %ERRORLEVEL%
)

echo Creating database...
createdb -h %DB_HOST% -p %DB_PORT% -U %DB_USER% %DB_NAME%
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to create database
    exit /b %ERRORLEVEL%
)

echo Applying schema...
psql -h %DB_HOST% -p %DB_PORT% -U %DB_USER% -d %DB_NAME% -f migrations/001_complete_schema.sql
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to apply schema
    exit /b %ERRORLEVEL%
)

echo Applying seed data...
psql -h %DB_HOST% -p %DB_PORT% -U %DB_USER% -d %DB_NAME% -f seeds/001_complete_seed_data.sql
if %ERRORLEVEL% NEQ 0 (
    echo Error: Failed to apply seed data
    exit /b %ERRORLEVEL%
)

echo Test database setup completed successfully
exit /b 0