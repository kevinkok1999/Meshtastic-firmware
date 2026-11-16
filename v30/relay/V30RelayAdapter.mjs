export class V30RelayAdapter {
  constructor(core, grantAuthority) {
    if (!core || !grantAuthority) throw new Error('adapter_dependency_missing');
    this.core = core;
    this.grantAuthority = grantAuthority;
  }

  accept(envelope, authorizationGrant, nowMs) {
    const verified = this.grantAuthority.verifyGrant(authorizationGrant, {
      scopeId: envelope?.scopeId,
      originId: envelope?.originId,
      right: 'push',
      nowMs,
    });
    return this.core.accept(envelope, nowMs, verified);
  }

  poll({ scopeId, consumerOriginId, nowMs, limit = 6, cursor = 0,
          authorizationGrant }) {
    const verified = this.grantAuthority.verifyGrant(authorizationGrant, {
      scopeId, originId: consumerOriginId, right: 'poll', nowMs,
    });
    return this.core.poll({
      scopeId, consumerOriginId, nowMs, limit, cursor,
      authorizationGrant: verified,
    });
  }
}
