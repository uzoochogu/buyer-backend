#!/bin/bash

# Database configuration
DB_NAME="buyer_app_test"
DB_USER="postgres"
DB_PASSWORD="yourpassword"
DB_HOST="127.0.0.1"
DB_PORT="5432"

# Export password for PostgreSQL commands
export PGPASSWORD="$DB_PASSWORD"

echo "Dropping database if it exists..."
dropdb --if-exists -h $DB_HOST -p $DB_PORT -U $DB_USER $DB_NAME || { echo "Error: Failed to drop database"; exit 1; }

echo "Creating database..."
createdb -h $DB_HOST -p $DB_PORT -U $DB_USER $DB_NAME || { echo "Error: Failed to create database"; exit 1; }

echo "Applying schema..."
psql -h $DB_HOST -p $DB_PORT -U $DB_USER -d $DB_NAME -f migrations/001_complete_schema.sql || { echo "Error: Failed to apply schema"; exit 1; }

echo "Applying seed data..."
psql -h $DB_HOST -p $DB_PORT -U $DB_USER -d $DB_NAME -f seeds/001_complete_seed_data.sql || { echo "Error: Failed to apply seed data"; exit 1; }

echo "Test database setup completed successfully"
exit 0