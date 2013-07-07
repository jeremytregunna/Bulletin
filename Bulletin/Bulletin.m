//
//  Bulletin.m
//  Bulletin
//
//  Created by Jeremy Tregunna on 2013-05-11.
//  Copyright (c) 2013 Jeremy Tregunna. All rights reserved.
//

#import "Bulletin.h"
#import "mosquitto.h"
#import "BulletinMessage.h"

static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    Bulletin* client = (__bridge Bulletin*)obj;
    if([client.delegate respondsToSelector:@selector(bulletin:didConnectWithCode:)])
        [client.delegate bulletin:client didConnectWithCode:rc];
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    Bulletin* client = (__bridge Bulletin*)obj;
    if([client.delegate respondsToSelector:@selector(bulletinDidDisconnect:)])
        [client.delegate bulletinDidDisconnect:client];
}

static void on_publish(struct mosquitto *mosq, void *obj, int message_id)
{
    Bulletin* client = (__bridge Bulletin*)obj;
    if([client.delegate respondsToSelector:@selector(bulletin:didPublishMessageWithID:)])
        [client.delegate bulletin:client didPublishMessageWithID:message_id];
}

static void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    BulletinMessage* msg = [[BulletinMessage alloc] init];
    msg.messageID = (uint16_t)message->mid;
    msg.topic = [NSString stringWithUTF8String:message->topic];
    msg.payload = [NSData dataWithBytes:message->payload length:message->payloadlen];
    msg.qualityOfService = (BulletinMessageServiceLevel)message->qos;
    msg.retained = (BOOL)message->retain;

    Bulletin* client = (__bridge Bulletin*)obj;

    if([client.delegate respondsToSelector:@selector(bulletin:didReceiveMessage:)])
        [client.delegate bulletin:client didReceiveMessage:msg];
}

static void on_subscribe(struct mosquitto *mosq, void *obj, int message_id, int qos_count, const int *granted_qos)
{
    Bulletin* client = (__bridge Bulletin*)obj;

    if([client.delegate respondsToSelector:@selector(bulletin:didSubscribeMessageID:qualityOfService:)])
        [client.delegate bulletin:client didSubscribeMessageID:message_id qualityOfService:*granted_qos];
}

static void on_unsubscribe(struct mosquitto *mosq, void *obj, int message_id)
{
    Bulletin* client = (__bridge Bulletin*)obj;
    if([client.delegate respondsToSelector:@selector(bulletin:didUnsubscribeMessageID:)])
        [client.delegate bulletin:client didUnsubscribeMessageID:message_id];
}

@implementation Bulletin
{
    struct mosquitto* _mosq;
    NSTimer* _timer;
}

+ (void)initialize
{
    mosquitto_lib_init();
}

+ (NSString*)version
{
    int major, minor, revision;
    mosquitto_lib_version(&major, &minor, &revision);
    return [NSString stringWithFormat:@"%d.%d.%d", major, minor, revision];
}

- (instancetype)initWithClientID:(NSString*)clientID delegate:(id<BulletinDelegate>)delegate
{
    if((self = [super init]))
    {
        const char* cstrClientId = [clientID UTF8String];
        _keepAlive = 60;
        _cleanSession = YES;
        _mosq = mosquitto_new(cstrClientId, _cleanSession, (__bridge void *)(self));
        mosquitto_connect_callback_set(_mosq, on_connect);
        mosquitto_disconnect_callback_set(_mosq, on_disconnect);
        mosquitto_publish_callback_set(_mosq, on_publish);
        mosquitto_message_callback_set(_mosq, on_message);
        mosquitto_subscribe_callback_set(_mosq, on_subscribe);
        mosquitto_unsubscribe_callback_set(_mosq, on_unsubscribe);
        _timer = nil;
        _delegate = delegate;
    }
    return self;
}

- (void)dealloc
{
    if(_mosq)
    {
        mosquitto_destroy(_mosq);
        _mosq = NULL;
    }

    if(_timer)
    {
        [_timer invalidate];
        _timer = nil;
    }
}

- (void)connectToBrokerWithURL:(NSURL*)brokerURL
{
    const char* user = NULL;
    const char* pw = NULL;

    if(_username != nil)
        user = [_username UTF8String];
    if(_password != nil)
        pw = [_password UTF8String];

    // TODO: error checking
    mosquitto_username_pw_set(_mosq, user, pw);

    mosquitto_connect(_mosq, [[brokerURL host] UTF8String], [[brokerURL port] intValue], _keepAlive);

    // FIXME: Really bad way of doing this. Rewrite libmosquitto in a more cocoa-ish way, and replace the "loop:" selector below with a block.
    _timer = [NSTimer scheduledTimerWithTimeInterval:0.03 target:self selector:@selector(loop:) userInfo:nil repeats:YES];
}

- (void)reconnect
{
    mosquitto_reconnect(_mosq);
}

- (void)disconnect
{
    mosquitto_disconnect(_mosq);
}

- (void)loop:(NSTimer*)timer
{
    mosquitto_loop(_mosq, 1, 1);
}

- (void)publishMessage:(NSData*)payload topic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos retained:(BOOL)retained
{
    const char* topicCString = [topic UTF8String];
    mosquitto_publish(_mosq, NULL, topicCString, (int)[payload length], [payload bytes], qos, retained);
}

- (void)subscribeToTopic:(NSString*)topic
{
    [self subscribeToTopic:topic qualityOfService:BulletinMessageServiceLevelNone];
}

- (void)subscribeToTopic:(NSString*)topic qualityOfService:(BulletinMessageServiceLevel)qos
{
    mosquitto_subscribe(_mosq, NULL, [topic UTF8String], qos);
}

- (void)unsubscribeFromTopic:(NSString*)topic
{
    mosquitto_unsubscribe(_mosq, NULL, [topic UTF8String]);
}

#pragma mark - Accessors

- (void)setMessageRetry:(NSUInteger)messageRetry
{
    [self willChangeValueForKey:@"messageRetry"];
    _messageRetry = messageRetry;
    mosquitto_message_retry_set(_mosq, (unsigned int)messageRetry);
    [self didChangeValueForKey:@"messageRetry"];
}

@end
