from flask import Flask, render_template, request, redirect, url_for, flash, jsonify
from flask_sqlalchemy import SQLAlchemy
from flask_login import LoginManager, UserMixin, login_user, login_required, logout_user, current_user
from werkzeug.security import generate_password_hash, check_password_hash
import pyotp
import time
import pytz
from datetime import datetime, timezone

app = Flask(__name__)
app.config['SECRET_KEY'] = 'your_super_secret_key'
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///database.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

# --- DATABASE MODELS ---


class User(UserMixin, db.Model):
    id = db.Column(db.Integer, primary_key=True)
    username = db.Column(db.String(150), unique=True, nullable=False)
    password = db.Column(db.String(150), nullable=False)
    role = db.Column(db.String(50), nullable=False)  # 'admin' or 'user'
    secret_code = db.Column(db.String(50), nullable=True)


class CodeLog(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    user_id = db.Column(db.Integer, nullable=False)
    username = db.Column(db.String(150), nullable=False)
    code = db.Column(db.String(10), nullable=False)
    timestamp = db.Column(
        db.DateTime, default=lambda: datetime.now(timezone.utc))


class Comment(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    user_id = db.Column(db.Integer, nullable=False)
    username = db.Column(db.String(150), nullable=False)
    text = db.Column(db.Text, nullable=False)
    timestamp = db.Column(
        db.DateTime, default=lambda: datetime.now(timezone.utc))


@login_manager.user_loader
def load_user(user_id):
    return db.session.get(User, int(user_id))

# --- ROUTES ---


@app.route('/', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        user = User.query.filter_by(username=username).first()

        if user and check_password_hash(user.password, password):
            login_user(user)
            if user.role == 'admin':
                return redirect(url_for('admin_dashboard'))
            else:
                return redirect(url_for('user_dashboard'))
        else:
            flash('Invalid credentials. Please try again.')
    return render_template('login.html')


@app.route('/admin_dashboard', methods=['GET', 'POST'])
@login_required
def admin_dashboard():
    if current_user.role != 'admin':
        return "Access Denied", 403

    if request.method == 'POST':
        new_username = request.form.get('username')
        new_password = request.form.get('password')

        if User.query.filter_by(role='user').count() >= 5:
            flash('Maximum limit of 5 users reached.', 'error')
        elif User.query.filter_by(username=new_username).first():
            flash('Username already exists.', 'error')
        else:
            hashed_pw = generate_password_hash(
                new_password, method='pbkdf2:sha256')
            secret = pyotp.random_base32()
            new_user = User(username=new_username,
                            password=hashed_pw, role='user', secret_code=secret)
            db.session.add(new_user)
            db.session.commit()
            flash(f'User {new_username} created successfully!', 'success')

    users = User.query.filter_by(role='user').all()
    logs = CodeLog.query.order_by(CodeLog.timestamp.desc()).all()
    comments = Comment.query.order_by(Comment.timestamp.desc()).all()
    return render_template('admin_dashboard.html', users=users, logs=logs, comments=comments)


@app.route('/delete_user/<int:user_id>', methods=['POST'])
@login_required
def delete_user(user_id):
    if current_user.role != 'admin':
        return "Access Denied", 403

    user_to_delete = User.query.get(user_id)
    if user_to_delete:
        db.session.delete(user_to_delete)
        db.session.commit()
        flash('User deleted successfully!', 'success')
    return redirect(url_for('admin_dashboard'))


@app.route('/delete_comment/<int:comment_id>', methods=['POST'])
@login_required
def delete_comment(comment_id):
    if current_user.role != 'admin':
        return "Access Denied", 403

    comment_to_delete = Comment.query.get(comment_id)
    if comment_to_delete:
        db.session.delete(comment_to_delete)
        db.session.commit()
        flash('Comment deleted successfully!', 'success')
    return redirect(url_for('admin_dashboard'))


@app.route('/user/change_password', methods=['POST'])
@login_required
def user_change_password():
    if current_user.role != 'user':
        return "Access Denied", 403

    current_pw = request.form.get('current_password')
    new_pw = request.form.get('new_password')
    confirm_pw = request.form.get('confirm_password')

    if not check_password_hash(current_user.password, current_pw):
        flash('Current password is incorrect.', 'danger')
        return redirect(url_for('user_dashboard'))

    if new_pw != confirm_pw:
        flash('New passwords do not match.', 'danger')
        return redirect(url_for('user_dashboard'))

    current_user.password = generate_password_hash(
        new_pw, method='pbkdf2:sha256')
    db.session.commit()
    flash('Password updated successfully!', 'success')
    return redirect(url_for('user_dashboard'))


@app.route('/user_dashboard', methods=['GET', 'POST'])
@login_required
def user_dashboard():
    if current_user.role != 'user':
        return "Access Denied", 403

    current_code = None
    if request.method == 'POST':
        action = request.form.get('action')
        if action == 'generate_code':
            totp = pyotp.TOTP(current_user.secret_code)
            current_code = totp.at(time.time())

            # FIX: Remove 'timestamp=datetime.now()'
            # The model will automatically use the lambda function you defined
            new_log = CodeLog(
                user_id=current_user.id,
                username=current_user.username,
                code=current_code
            )

            try:
                db.session.add(new_log)
                db.session.commit()  # This makes it visible to the Admin
                flash('New security code generated and logged!', 'success')
            except Exception as e:
                db.session.rollback()
                flash('Error logging the code request.', 'danger')

        elif action == 'send_comment':
            comment_text = request.form.get('comment_text')
            if comment_text:
                new_comment = Comment(
                    user_id=current_user.id,
                    username=current_user.username,
                    text=comment_text
                )
                db.session.add(new_comment)
                db.session.commit()
                flash('Comment sent to admin!', 'success')

    return render_template('user_dashboard.html', code=current_code)


@app.route('/api/sync_users', methods=['GET'])
def sync_users():
    try:
        users = User.query.filter_by(role='user').all()
        user_list = [{"name": u.username, "secret": u.secret_code}
                     for u in users]
        print(f"Syncing {len(user_list)} users to ESP32.")
        return jsonify(user_list)
    except Exception as e:
        print(f"Sync Error: {e}")
        return jsonify({"error": str(e)}), 500


@app.route('/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('login'))


if __name__ == '__main__':
    with app.app_context():
        db.create_all()
        if not User.query.filter_by(role='admin').first():
            hashed_pw = generate_password_hash(
                'admin123', method='pbkdf2:sha256')
            admin = User(username='admin', password=hashed_pw, role='admin')
            db.session.add(admin)
            db.session.commit()
            print("Default admin created: 'admin' / 'admin123'")

    app.run(host='0.0.0.0', port=5000)
