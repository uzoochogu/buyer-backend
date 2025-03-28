INSERT INTO users (username, password_hash) VALUES ('user1', 'hash1');
INSERT INTO users (username, password_hash) VALUES ('user2', 'hash2');

INSERT INTO orders (user_id, status) VALUES (1, 'in_progress');
INSERT INTO orders (user_id, status) VALUES (2, 'delivered');
INSERT INTO orders (user_id, status) VALUES (1, 'pending');
INSERT INTO orders (user_id, status) VALUES (2, 'in_progress');
INSERT INTO orders (user_id, status) VALUES (1, 'cancelled');
INSERT INTO orders (user_id, status) VALUES (2, 'delivered');
INSERT INTO orders (user_id, status) VALUES (1, 'pending');
INSERT INTO orders (user_id, status) VALUES (2, 'in_transit');
INSERT INTO orders (user_id, status) VALUES (1, 'delivered');
INSERT INTO orders (user_id, status) VALUES (2, 'pending');

INSERT INTO posts (user_id, content) VALUES (1, 'First post!');
INSERT INTO posts (user_id, content) VALUES (2, 'Hello world!');

INSERT INTO chats (user_id, message) VALUES (1, 'Hi there!');
INSERT INTO chats (user_id, message) VALUES (2, 'Hello!');