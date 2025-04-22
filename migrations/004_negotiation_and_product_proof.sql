-- Add negotiation status to offers table
ALTER TABLE offers
ADD COLUMN negotiation_status VARCHAR(50) DEFAULT 'none';
-- none, in_progress, completed

-- Add original_price column to offers table
ALTER TABLE offers ADD COLUMN original_price DECIMAL(10, 2);

-- Copy existing price data to original_price for existing records
UPDATE offers
SET
    original_price = price
WHERE
    original_price IS NULL;

-- Make original_price NOT NULL for future records
ALTER TABLE offers ALTER COLUMN original_price SET NOT NULL;

-- Create table for product proofs
CREATE TABLE product_proofs (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    image_url TEXT NOT NULL,
    description TEXT,
    status VARCHAR(50) DEFAULT 'pending', -- pending, approved, rejected
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Create table for price negotiations
CREATE TABLE price_negotiations (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    user_id INT REFERENCES users (id) ON DELETE CASCADE,
    proposed_price DECIMAL(10, 2) NOT NULL,
    status VARCHAR(50) DEFAULT 'pending', -- pending, accepted, rejected
    message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Add product context to messages
ALTER TABLE messages
ADD COLUMN context_type VARCHAR(50),
ADD COLUMN context_id INT;

ADD COLUMN metadata JSONB DEFAULT '{}'::jsonb

-- Create table for escrow transactions
CREATE TABLE escrow_transactions (
    id SERIAL PRIMARY KEY,
    offer_id INT REFERENCES offers (id) ON DELETE CASCADE,
    buyer_id INT REFERENCES users (id) ON DELETE CASCADE,
    seller_id INT REFERENCES users (id) ON DELETE CASCADE,
    amount DECIMAL(10, 2) NOT NULL,
    status VARCHAR(50) DEFAULT 'pending', -- pending, funded, released, refunded
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Add indexes for performance
CREATE INDEX product_proofs_offer_id_idx ON product_proofs (offer_id);

CREATE INDEX price_negotiations_offer_id_idx ON price_negotiations (offer_id);

CREATE INDEX escrow_transactions_offer_id_idx ON escrow_transactions (offer_id);

CREATE INDEX messages_context_idx ON messages (context_type, context_id);